# Zephyr 4.3 Comprehensive Demo - Developer Guide

This guide provides detailed information about the Zephyr 4.3 features showcased in this demo.

## Architecture

The demo is structured into several modules:

```
ocpp_demo_zephyr43/
├── src/
│   ├── main.c           # Main application and OCPP integration
│   ├── gui.c            # LVGL GUI implementation
│   ├── cpu_monitor.c    # CPU load monitoring
│   ├── usb_status.c     # USB device status tracking
│   └── ocpp_demo.h      # Shared definitions
├── boards/
│   ├── stm32f746g_disco.conf     # Board-specific config
│   └── stm32f746g_disco.overlay  # Device tree overlay
└── prj.conf             # Project configuration
```

## Zephyr 4.3 Features Demonstrated

### 1. OCPP 1.6 Library

The demo implements a complete OCPP (Open Charge Point Protocol) charge point with:
- Multiple connector support (2 connectors)
- Authorization and transaction management
- Meter value reporting
- Remote start/stop capability
- WebSocket communication with Central System

**Code Location**: `src/main.c` - see `ocpp_cp_entry()` and `user_notify_cb()`

### 2. USB Device "Next" Stack

The new USB device stack is initialized with:
- Runtime configuration
- Status callbacks for connection monitoring
- Better architecture and multiple controller support

**Code Location**: `src/usb_status.c`

**Configuration**:
```
CONFIG_USB_DEVICE_STACK=y
CONFIG_USB_DEVICE_VID=0x2FE3
CONFIG_USB_DEVICE_PID=0x0100
```

### 3. CPU Load Subsystem

Real-time CPU usage monitoring using scheduler statistics:
- Periodic sampling (every 1 second)
- Percentage calculation (0-100%)
- Visual display on GUI

**Code Location**: `src/cpu_monitor.c`

**Configuration**:
```
CONFIG_CPU_LOAD=y
CONFIG_SCHED_THREAD_USAGE=y
```

### 4. CPU Frequency Scaling

The demo shows CPU frequency information. On STM32F746, the frequency is fixed at 216 MHz, but the framework is ready for platforms that support dynamic frequency scaling.

**Configuration** (when available on platform):
```
CONFIG_CPU_FREQ=y
CONFIG_CPU_FREQ_GOVERNOR_PERFORMANCE=y  # or ON_DEMAND
```

### 5. LVGL GUI with Touchscreen

Interactive graphical user interface displaying:
- System status (network, OCPP, USB)
- CPU load bar with color coding
- Two connector panels with real-time status
- Meter readings and transaction information
- System uptime

**Code Location**: `src/gui.c`

**Key Features**:
- 480x272 pixel display
- Capacitive touch input
- Real-time updates
- Color-coded status indicators

### 6. Instrumentation Subsystem (Ready)

The application is structured to support compiler-based instrumentation for tracing and profiling.

**To Enable**:
```
CONFIG_INSTRUMENTATION=y
CONFIG_INSTRUMENTATION_BUFFER_SIZE=4096
```

**Usage**:
```bash
# Collect traces
scripts/instrumentation/zaru.py trace -v -c function_name

# Get profile
scripts/instrumentation/zaru.py profile -v -n 10

# Export to Perfetto
scripts/instrumentation/zaru.py trace -v --perfetto --output trace.json
```

## Building the Demo

### Prerequisites

1. Zephyr SDK installed
2. West tool available
3. STM32F746G Discovery Kit
4. Network connection (Ethernet cable)
5. OCPP Central System server (e.g., SteVe)

### Build Steps

```bash
# Set server addresses
export OCPP_SERVER="your-server-ip"
export SNTP_SERVER="pool.ntp.org"

# Build
west build -b stm32f746g_disco samples/net/ocpp_demo_zephyr43 -- \
  -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"${OCPP_SERVER}\" \
  -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"${SNTP_SERVER}\"

# Flash
west flash
```

### Alternative: Configure in prj.conf

Edit `prj.conf` and set:
```
CONFIG_NET_SAMPLE_SNTP_SERVER="pool.ntp.org"
CONFIG_NET_SAMPLE_OCPP_SERVER="192.168.1.100"
CONFIG_NET_SAMPLE_OCPP_PORT=8180
```

## Developer Experience Tools

### 1. DTDoctor - Devicetree Diagnostics

Diagnose devicetree build errors:

```bash
west dtdoctor
```

This helps identify and fix devicetree configuration issues.

### 2. Trace Kconfig - Configuration Analysis

Understand where Kconfig symbols come from:

```bash
west build -t traceconfig
```

This creates an interactive view of configuration dependencies.

### 3. Footprint Analysis - Memory Visualization

Interactive charts for RAM/ROM usage:

```bash
west build -t footprint
```

This generates visual reports of memory usage by component.

### 4. Code Size Analysis

```bash
west build -t rom_report
west build -t ram_report
```

## GUI Customization

The GUI can be customized by modifying `src/gui.c`:

### Colors

```c
#define COLOR_AVAILABLE   lv_palette_main(LV_PALETTE_GREEN)
#define COLOR_CHARGING    lv_palette_main(LV_PALETTE_BLUE)
// ... modify as needed
```

### Layout

Adjust positions in `gui_init()`:
```c
lv_obj_set_pos(connector_panels[i], x_pos, y_pos);
lv_obj_set_size(connector_panels[i], 225, 160);
```

### Fonts

Change fonts in prj.conf:
```
CONFIG_LV_FONT_MONTSERRAT_24=y
```

## Performance Tuning

### Increase GUI Update Rate

In `src/main.c`:
```c
k_timer_start(&gui_update_timer, K_SECONDS(1), K_MSEC(250)); // Update every 250ms
```

### Adjust Memory Pools

In `prj.conf`:
```
CONFIG_LV_Z_MEM_POOL_SIZE=65536  # Increase for more complex UI
CONFIG_HEAP_MEM_POOL_SIZE=65536  # Increase for more network buffers
```

### Network Tuning

```
CONFIG_NET_PKT_RX_COUNT=56       # Double default
CONFIG_NET_BUF_RX_COUNT=120      # Double default
```

## Debugging

### Enable Debug Logging

```
CONFIG_LOG_MODE_IMMEDIATE=y      # Immediate logging
CONFIG_OCPP_LOG_LEVEL_DBG=y     # OCPP debug logs
CONFIG_LVGL_LOG_LEVEL_DBG=y     # LVGL debug logs
```

### Shell Commands

The demo includes shell support. Connect via UART and use:

```
uart:~$ kernel threads       # List threads
uart:~$ kernel stacks        # Stack usage
uart:~$ net iface            # Network interfaces
uart:~$ net stats            # Network statistics
uart:~$ lvgl stats           # LVGL statistics
```

## Testing Without Central System

For testing the GUI and system monitoring without an OCPP server:

1. Comment out OCPP initialization in `main.c`
2. Build and flash
3. The GUI will still show system status, CPU load, and USB

## Common Issues

### Display Not Working

Check:
1. Display is enabled in devicetree
2. LTDC peripheral is configured
3. SDRAM is initialized for framebuffers

### Touch Not Responding

Verify:
1. I2C3 bus is enabled
2. FT5336 touch controller is detected
3. LVGL pointer input is configured

### USB Not Connecting

Ensure:
1. USB OTG FS is enabled
2. Correct USB device stack is selected
3. USB cable is connected to OTG port

### Network Issues

Verify:
1. Ethernet cable is connected
2. DHCP is working (check logs)
3. Server IP is reachable

## Future Enhancements

Potential additions to the demo:

1. **Dynamic Frequency Scaling**: When platform supports it, add actual frequency changes based on load
2. **More USB Classes**: Add CDC ACM or HID for richer USB demonstration
3. **Charts**: Add LVGL charts for historical CPU load and meter values
4. **Settings Screen**: Touch-based configuration UI
5. **Perfetto Integration**: Real-time trace streaming
6. **Power Management**: Demonstrate low-power modes

## References

- [OCPP Specification](https://www.openchargealliance.org/)
- [LVGL Documentation](https://docs.lvgl.io)
- [Zephyr USB Device Documentation](https://docs.zephyrproject.org/latest/connectivity/usb/device/)
- [CPU Load Subsystem](https://docs.zephyrproject.org/latest/services/debugging/cpu_load.html)
- [Instrumentation Subsystem](https://docs.zephyrproject.org/latest/services/debugging/instrumentation.html)
