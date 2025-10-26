# OCPP CitrineOS Demo - Complete Guide

This directory contains a complete demonstration of connecting a Zephyr-based OCPP charge point to CitrineOS, an open-source OCPP Central System.

## What is CitrineOS?

[CitrineOS](https://github.com/citrineos/citrineos-core) is a modern, cloud-native OCPP Central System that supports both OCPP 1.6 and OCPP 2.0.1. It provides a complete solution for managing electric vehicle charging stations.

## What's Included

This demo provides multiple ways to build and run the OCPP charge point with CitrineOS:

### Source Files
- **`src/main.c`** - Standard OCPP demo (compatible with SteVe and other OCPP servers)
- **`src/main_citrineos.c`** - CitrineOS-optimized demo with specific configurations

### Configuration Files
- **`prj.conf`** - Base OCPP configuration (works with SteVe)
- **`prj_citrineos.conf`** - Complete standalone configuration for CitrineOS
- **`overlay-citrineos.conf`** - Minimal overlay to adapt base config for CitrineOS

### Documentation
- **`README_CITRINEOS.rst`** - Comprehensive setup and usage guide
- **`CITRINEOS_QUICKSTART.md`** - Quick start guide with step-by-step instructions
- **`ARCHITECTURE.md`** - System architecture and design documentation
- **`README.md`** - This file

### Helper Scripts
- **`build_citrineos.sh`** - Example build script for quick setup

## Quick Start

### 1. Start CitrineOS

```bash
git clone https://github.com/citrineos/citrineos-core.git
cd citrineos-core
docker-compose up -d
```

### 2. Build the Demo

**Option A: Using the build script**
```bash
cd samples/net/ocpp
export CITRINEOS_IP=192.168.1.100  # Your CitrineOS IP
export BOARD=stm32f769i_disco       # Your target board
./build_citrineos.sh
```

**Option B: Manual build with overlay**
```bash
west build -b stm32f769i_disco samples/net/ocpp -- \
  -DOVERLAY_CONFIG=overlay-citrineos.conf \
  -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"192.168.1.100\" \
  -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"pool.ntp.org\"
```

**Option C: Using dedicated CitrineOS source**
```bash
west build -b stm32f769i_disco samples/net/ocpp -- \
  -DOVERLAY_CONFIG=overlay-citrineos.conf \
  -DUSE_CITRINEOS_DEMO=y \
  -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"192.168.1.100\" \
  -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"pool.ntp.org\"
```

**Option D: Using standalone CitrineOS config**
```bash
west build -b stm32f769i_disco samples/net/ocpp -- \
  -DCONF_FILE=prj_citrineos.conf \
  -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"192.168.1.100\" \
  -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"pool.ntp.org\"
```

### 3. Flash and Run

```bash
west flash
# Connect to serial console
screen /dev/ttyACM0 115200
```

### 4. Monitor in CitrineOS

1. Open http://localhost:8080 in your browser
2. Navigate to Charge Points
3. Find "CitrineOSDemo" or "zephyr"
4. Monitor transactions and meter values

## Key Differences: CitrineOS vs SteVe

| Feature | CitrineOS | SteVe |
|---------|-----------|-------|
| Port | 8080 | 8180 |
| WebSocket Path | `/ocpp` | `/steve/websocket/CentralSystemService/<id>` |
| Setup | Docker Compose | Java WAR |
| UI | Modern React | Traditional JSP |
| Protocol Support | OCPP 1.6 & 2.0.1 | OCPP 1.6 |

## Documentation Structure

For different needs, choose the appropriate documentation:

- **New to OCPP?** Start with `README_CITRINEOS.rst` for comprehensive overview
- **Want to get running quickly?** Use `CITRINEOS_QUICKSTART.md`
- **Need to understand the code?** Read `ARCHITECTURE.md`
- **Looking for build options?** Check `build_citrineos.sh`

## Features Demonstrated

- [x] Charge Point registration with CitrineOS
- [x] Boot notification
- [x] Heartbeat mechanism
- [x] ID tag authorization
- [x] Starting charging transactions
- [x] Meter value reporting (simulated)
- [x] Stopping charging transactions
- [x] Remote start/stop from CitrineOS
- [x] Multiple connector support (2 connectors)
- [x] Thread-based connector management
- [x] ZBus message passing for inter-thread communication

## Supported Boards

Any Zephyr board with network capability:
- STM32F769I Discovery (tested)
- QEMU x86 (for testing)
- ESP32 (with WiFi)
- nRF boards (with Ethernet/WiFi)
- And more...

## Extending the Demo

The demo can be extended to:

1. **Add more connectors** - Increase `NO_OF_CONN`
2. **Real meter integration** - Implement actual hardware meter reading
3. **Firmware updates** - Add OTA support via OCPP
4. **Smart charging** - Implement charging profiles
5. **OCPP 2.0.1** - Upgrade to newer protocol (CitrineOS supports both)

## Troubleshooting

### Cannot connect to CitrineOS
- Verify CitrineOS is running: `docker ps`
- Check firewall allows port 8080
- Ensure correct IP address

### DHCP failure
- Check network cable
- Verify DHCP server available
- Try static IP if needed

### Authorization rejected
- Check CitrineOS logs: `docker logs citrineos`
- Verify ID tag configuration in CitrineOS
- Some configurations require pre-registration

## Resources

- [CitrineOS Documentation](https://github.com/citrineos/citrineos-core)
- [OCPP 1.6 Specification](https://www.openchargealliance.org/)
- [Zephyr OCPP API](https://docs.zephyrproject.org/latest/connectivity/networking/api/ocpp.html)
- [Zephyr Getting Started](https://docs.zephyrproject.org/latest/develop/getting_started/)

## License

SPDX-License-Identifier: Apache-2.0

Copyright (c) 2025 Linumiz GmbH
