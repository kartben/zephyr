# Quick Start Guide - Zephyr 4.3 OCPP Demo

Get the demo running in 5 minutes!

## What You Need

### Hardware
- ✅ STM32F746G Discovery Kit
- ✅ Ethernet cable (RJ45)
- ✅ USB cable (Micro-B for programming, Micro-AB for OTG)
- ✅ Power supply (USB or external)

### Software
- ✅ Zephyr SDK 0.16.0 or later
- ✅ West tool installed
- ✅ OCPP Central System server (SteVe recommended)

### Network
- ✅ Router/switch with DHCP
- ✅ Access to NTP server (internet or local)

## Step 1: Setup OCPP Server (SteVe)

### Quick SteVe Setup with Docker

```bash
# Run SteVe server
docker run -d \
  --name steve \
  -p 8080:8080 \
  -p 8180:8180 \
  -p 8443:8443 \
  steve/steve:latest

# Access web interface
open http://localhost:8080/steve/manager
# Default login: admin / 1234
```

### Add Charge Point in SteVe

1. Navigate to "Data Management" → "Charge Points"
2. Click "Add"
3. Set Charge Box ID: `zephyr`
4. Save

## Step 2: Configure the Demo

Edit `prj.conf` in the sample directory:

```bash
cd samples/net/ocpp_demo_zephyr43
nano prj.conf
```

Update these lines with your network info:

```
CONFIG_NET_SAMPLE_SNTP_SERVER="pool.ntp.org"
CONFIG_NET_SAMPLE_OCPP_SERVER="192.168.1.100"  # Your SteVe server IP
CONFIG_NET_SAMPLE_OCPP_PORT=8180
```

**💡 Tip**: Find your server IP with `ip addr` (Linux) or `ipconfig` (Windows)

## Step 3: Build and Flash

```bash
# From Zephyr workspace root
west build -b stm32f746g_disco samples/net/ocpp_demo_zephyr43

# Flash to board
west flash
```

**Alternative with CMake GUI:**
```bash
ccmake -B build -S samples/net/ocpp_demo_zephyr43 -DBOARD=stm32f746g_disco
# Edit CONFIG_ values in GUI
cmake --build build
```

## Step 4: Connect Hardware

1. **Connect Ethernet**: Plug cable into RJ45 port on Discovery board
2. **Connect USB**: 
   - Micro-B to ST-LINK (for programming/debug)
   - Micro-AB to OTG port (for USB device demo)
3. **Power on**: Board should boot immediately

## Step 5: Verify It's Working

### On the Display (480x272 LCD)

You should see:
```
┌─────────────────────────────────────────┐
│      Zephyr 4.3 OCPP Demo               │
├─────────────────────────────────────────┤
│ NET: 192.168.1.x   OCPP: Online  USB: Yes│
│ CPU: ████░░░░░░ 40%  216MHz              │
├───────────────────┬─────────────────────┤
│   Connector 1     │    Connector 2      │
│   State: Charging │    State: Available │
│   Meter: 125 Wh   │    Meter: 0 Wh      │
│   ID: ZepId00     │    ID: --           │
│   Txn: 12345      │    Txn: --          │
└───────────────────┴─────────────────────┘
```

### On the Serial Console

```bash
# Connect to serial (115200 baud)
minicom -D /dev/ttyACM0 -b 115200
# or
screen /dev/ttyACM0 115200
```

Expected output:
```
*** Booting Zephyr OS build v4.3.0 ***
================================================
  Zephyr 4.3 OCPP Demo - STM32F746G Discovery  
================================================
Showcasing Zephyr 4.3 Highlights:
  - OCPP 1.6 Library
  - USB Device 'Next' Stack
  - CPU Load Monitoring
  - LVGL GUI with Touchscreen
  - Instrumentation Ready
================================================

[00:00:00.100] Display initialized: 480x272
[00:00:00.150] USB Device stack initialized
[00:00:01.000] CPU load monitoring started
[00:00:02.642] Network: 192.168.1.101
[00:00:07.024] OCPP: Connected to Central System
```

### In SteVe Web Interface

1. Go to "Data Management" → "Charge Points"
2. Status should show: ✅ **Online**
3. Last seen: **Just now**

## Step 6: Interact with the Demo

### Using the Touchscreen

- **Tap** on connector panels to view details
- **Watch** real-time CPU load bar
- **Monitor** network and USB status

### From SteVe (Remote Control)

#### Start a Charging Session
1. Go to "Operations" → "Remote Start Transaction"
2. Select Charge Point: `zephyr`
3. Connector ID: `1` or `2`
4. ID Tag: `ZepId00`
5. Click "Start"

▶️ **Watch the display update to "Charging" state!**

#### Stop a Charging Session
1. Go to "Operations" → "Remote Stop Transaction"
2. Select the active transaction
3. Click "Stop"

⏹️ **Session ends, connector returns to "Available"**

## What's Happening Behind the Scenes

### OCPP Messages
```
➡️  BootNotification: Announce to Central System
⬅️  BootNotification Response: Accepted
➡️  Heartbeat: Regular keep-alive
➡️  Authorize: Check ID tag
⬅️  Authorize Response: Accepted
➡️  StartTransaction: Begin charging
➡️  MeterValues: Periodic meter readings
➡️  StopTransaction: End session
```

### CPU Load Monitoring
- Samples every 1 second
- Shows % of CPU time used by threads
- Color coded: Green (0-50%), Yellow (50-80%), Red (80-100%)

### USB Device Stack
- Enumerates as USB device when connected
- Status shown in real-time on display
- Demonstrates new "Next" architecture

## Troubleshooting

### 🔴 "NET: Disconnected"
**Problem**: No DHCP address

**Solutions**:
1. Check Ethernet cable is connected
2. Verify router/switch is working
3. Check logs: `dmesg | grep eth`
4. Try static IP in `prj.conf`:
   ```
   CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.1.50"
   CONFIG_NET_CONFIG_MY_IPV4_GW="192.168.1.1"
   ```

### 🔴 "OCPP: Offline"
**Problem**: Can't connect to Central System

**Solutions**:
1. Verify server IP is correct
2. Check port 8180 is open: `telnet 192.168.1.100 8180`
3. Ensure SteVe is running
4. Check firewall settings
5. Verify network connectivity: `ping 192.168.1.100`

### 🔴 Display is blank
**Problem**: LTDC or display init failed

**Solutions**:
1. Check power supply is adequate
2. Verify display cable is connected
3. Check logs for LTDC errors
4. Try: `west build -t menuconfig` → Enable `CONFIG_DISPLAY_LOG_LEVEL_DBG`

### 🔴 Touch not working
**Problem**: FT5336 touch controller not responding

**Solutions**:
1. Check I2C3 initialization in logs
2. Verify touch controller address (0x38)
3. Test with LVGL shell: `lvgl stats`

### 🔴 USB not detected
**Problem**: USB device not enumerating

**Solutions**:
1. Use correct USB port (Micro-AB OTG port)
2. Check cable supports data (not just charging)
3. Verify host PC recognizes device
4. On Linux: `lsusb | grep Zephyr`
5. Enable debug: `CONFIG_USB_DEVICE_DRIVER_LOG_LEVEL_DBG=y`

## Next Steps

### 📊 View CPU Load History
Add LVGL charts to track CPU usage over time

### 🔋 Add Power Management
Demonstrate low-power modes with sleep/wake

### 📈 Instrumentation Profiling
```bash
# Enable instrumentation
west build -- -DCONFIG_INSTRUMENTATION=y

# Collect and visualize traces
scripts/instrumentation/zaru.py trace -v
```

### 🎨 Customize the GUI
Edit `src/gui.c` to change colors, layout, or add features

### 📱 Add More USB Classes
Implement CDC ACM or HID for richer USB functionality

## Getting Help

- **Zephyr Discord**: https://chat.zephyrproject.org/
- **GitHub Issues**: https://github.com/zephyrproject-rtos/zephyr/issues
- **Documentation**: https://docs.zephyrproject.org/
- **OCPP Spec**: https://www.openchargealliance.org/protocols/ocpp-16/

## Demo Checklist

- [ ] SteVe server running and accessible
- [ ] Board powered and connected to network
- [ ] Correct server IP in configuration
- [ ] Built and flashed successfully
- [ ] Display shows "NET: Connected"
- [ ] Display shows "OCPP: Online"
- [ ] Can trigger remote start from SteVe
- [ ] Can trigger remote stop from SteVe
- [ ] CPU load bar is animating
- [ ] Touch screen is responsive
- [ ] USB device is detected (if cable connected)

🎉 **Congratulations! You're showcasing Zephyr 4.3!** 🎉
