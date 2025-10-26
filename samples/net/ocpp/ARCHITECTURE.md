OCPP CitrineOS Demo Architecture
==================================

System Overview
---------------

This document describes the architecture of the Zephyr OCPP CitrineOS demo.

```
┌──────────────────────────────────────────────────────────────┐
│                    CitrineOS Central System                  │
│                      (Docker Container)                       │
│                                                               │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │  Web UI     │  │  OCPP Server │  │   PostgreSQL    │   │
│  │ (Port 8080) │  │   (Port 8080)│  │    Database     │   │
│  └─────────────┘  └──────────────┘  └──────────────────┘   │
│         │                │                    │              │
│         └────────────────┴────────────────────┘              │
└──────────────────────────┬───────────────────────────────────┘
                           │
                           │ WebSocket (OCPP 1.6)
                           │ ws://<ip>:8080/ocpp
                           │
┌──────────────────────────┴───────────────────────────────────┐
│                Zephyr OCPP Charge Point                       │
│                  (Target Hardware)                            │
│                                                               │
│  ┌───────────────────────────────────────────────────────┐   │
│  │              Application Layer                         │   │
│  │  ┌──────────────┐  ┌──────────────┐                   │   │
│  │  │ main.c or    │  │  User        │                   │   │
│  │  │ main_citrine │  │  Callbacks   │                   │   │
│  │  │   os.c       │  │              │                   │   │
│  │  └──────────────┘  └──────────────┘                   │   │
│  └───────────────────────────────────────────────────────┘   │
│                           │                                   │
│  ┌───────────────────────────────────────────────────────┐   │
│  │              OCPP Library (Zephyr)                     │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐    │   │
│  │  │ Protocol │  │ WebSocket│  │  Message Queue   │    │   │
│  │  │  Handler │  │  Client  │  │  (ZBus)          │    │   │
│  │  └──────────┘  └──────────┘  └──────────────────┘    │   │
│  └───────────────────────────────────────────────────────┘   │
│                           │                                   │
│  ┌───────────────────────────────────────────────────────┐   │
│  │          Networking Stack (Zephyr)                     │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐            │   │
│  │  │   TCP    │  │  DHCP    │  │   SNTP   │            │   │
│  │  └──────────┘  └──────────┘  └──────────┘            │   │
│  └───────────────────────────────────────────────────────┘   │
│                           │                                   │
│  ┌───────────────────────────────────────────────────────┐   │
│  │         Ethernet/WiFi Driver                           │   │
│  └───────────────────────────────────────────────────────┘   │
└───────────────────────────┬───────────────────────────────────┘
                            │
                            │ Ethernet/WiFi
                            │
                    ┌───────┴────────┐
                    │   Network      │
                    │   Router/DHCP  │
                    └────────────────┘
```

Component Interactions
----------------------

### 1. Initialization Sequence

```
main() -> wait_for_network() -> DHCP
      |
      +-> ocpp_getaddrinfo() -> DNS resolution
      |
      +-> ocpp_get_time_from_sntp() -> Time sync
      |
      +-> ocpp_init() -> Connect to CitrineOS
                      -> Send BootNotification
                      -> Start heartbeat
```

### 2. Charging Session Flow

```
Thread Start
    |
    +-> ocpp_session_open()
    |
    +-> ocpp_authorize(idtag) -> CitrineOS validates
    |
    +-> ocpp_start_transaction() -> CitrineOS accepts
    |
    +-> [Charging Active - periodic meter values]
    |
    +-> ocpp_stop_transaction() -> CitrineOS records
    |
    +-> ocpp_session_close()
```

### 3. Remote Operations (from CitrineOS)

```
CitrineOS UI
    |
    +-> Remote Start Transaction
            |
            +-> OCPP Message to Zephyr
            |
            +-> user_notify_cb(OCPP_USR_START_CHARGING)
            |
            +-> Create charging thread
            |
            +-> Start transaction flow
```

Key Files and Their Roles
--------------------------

### Application Files

- **main.c**: Standard OCPP demo (SteVe compatible)
- **main_citrineos.c**: CitrineOS-specific demo with tailored settings

### Configuration Files

- **prj.conf**: Base OCPP configuration
- **prj_citrineos.conf**: Complete CitrineOS configuration (standalone)
- **overlay-citrineos.conf**: CitrineOS-specific overlays (minimal)

### Documentation Files

- **README.rst**: Main README (SteVe focused)
- **README_CITRINEOS.rst**: CitrineOS-specific documentation
- **CITRINEOS_QUICKSTART.md**: Quick start guide
- **ARCHITECTURE.md**: This file

### Build Files

- **CMakeLists.txt**: Build configuration with CitrineOS source option
- **sample.yaml**: Test configuration including CitrineOS variant
- **build_citrineos.sh**: Example build script

Threading Model
---------------

The demo uses multiple threads:

1. **Main Thread**: Initialization and coordination
2. **OCPP Library Threads**: Internal protocol handling
3. **Connector Threads** (2): One per connector, handle:
   - Authorization
   - Transaction start/stop
   - Meter value reporting

Threads communicate via ZBus message channels.

Configuration Options
---------------------

### Key Kconfig Options

- `CONFIG_OCPP=y`: Enable OCPP library
- `CONFIG_NET_SAMPLE_OCPP_SERVER`: CitrineOS IP address
- `CONFIG_NET_SAMPLE_OCPP_PORT`: CitrineOS port (8080)
- `CONFIG_WEBSOCKET_CLIENT=y`: Enable WebSocket client
- `CONFIG_HTTP_CLIENT=y`: Enable HTTP client

### Build-time Selections

- `USE_CITRINEOS_DEMO=y`: Use main_citrineos.c instead of main.c
- `OVERLAY_CONFIG=overlay-citrineos.conf`: Apply CitrineOS overlay
- `CONF_FILE=prj_citrineos.conf`: Use standalone CitrineOS config

Data Flow
---------

### Boot Notification

```
Zephyr                          CitrineOS
  |                                |
  +-- BootNotification Request --> |
  |   (charge point info)          |
  |                                |
  | <-- BootNotification Response -+
  |     (Accepted, heartbeat=60)   |
  |                                |
  +-- Heartbeat every 60s -------> |
```

### Authorization and Transaction

```
Zephyr                          CitrineOS
  |                                |
  +-- Authorize Request ---------> |
  |   (idTag: "CitrineID00")       |
  |                                |
  | <-- Authorize Response --------+
  |     (Accepted)                 |
  |                                |
  +-- StartTransaction ----------> |
  |   (connectorId=1, idTag)       |
  |                                |
  | <-- StartTransaction Response -+
  |     (transactionId=123)        |
  |                                |
  +-- MeterValues ---------------> |
  |   (periodic updates)           |
  |                                |
  +-- StopTransaction -----------> |
  |   (transactionId=123)          |
  |                                |
  | <-- StopTransaction Response --+
  |     (Accepted)                 |
```

Extensibility
-------------

The demo can be extended to:

1. **Add More Connectors**: Increase `NO_OF_CONN` in source
2. **Real Meter Integration**: Implement actual meter reading in callback
3. **Firmware Updates**: Add support for OCPP firmware update messages
4. **Configuration Management**: Support remote configuration changes
5. **Smart Charging**: Implement charging profiles
6. **OCPP 2.0.1**: Upgrade to newer protocol version

Security Considerations
-----------------------

This demo uses:
- Unencrypted WebSocket (ws://)
- No authentication tokens

For production:
- Use WSS (WebSocket Secure)
- Implement certificate-based authentication
- Add TLS configuration
- Secure storage for credentials

Performance Characteristics
---------------------------

- **Memory Usage**: ~15KB heap
- **Stack Size**: 4KB main, 2KB per connector thread
- **Network Buffers**: 28 RX packets, 60 buffers
- **Response Time**: <500ms for most operations
- **Heartbeat Interval**: 60 seconds (configurable)

References
----------

- OCPP 1.6 Specification: https://www.openchargealliance.org/
- CitrineOS GitHub: https://github.com/citrineos/citrineos-core
- Zephyr Networking: https://docs.zephyrproject.org/latest/connectivity/networking/
- WebSocket RFC 6455: https://tools.ietf.org/html/rfc6455
