# OCPP Demo Quick Start Guide

This guide will help you get the OCPP EV Charging Station demo up and running quickly.

## What This Demo Does

This demo simulates a realistic electric vehicle charging station with:
- **2 charging connectors** (one fast, one normal)
- **Real-time meter values** (power, energy, current, voltage, temperature, battery %)
- **Dynamic charging simulation** with power ramp-up and temperature changes
- **Connection to a public OCPP server** at cs.ocpp-css.com
- **Remote control** capabilities from the Central System

## Prerequisites

- Zephyr development environment set up
- Network-capable board OR native simulation support
- Internet connectivity (to reach cs.ocpp-css.com)

## Quick Start (3 Steps)

### Step 1: Build the Demo

For **native simulation** (easiest to try):
```bash
west build -b native_sim samples/net/ocpp_demo
```

For **STM32F769I Discovery** board:
```bash
west build -b stm32f769i_disco samples/net/ocpp_demo
west flash
```

For **other boards**:
```bash
west build -b <your_board> samples/net/ocpp_demo
west flash
```

### Step 2: Run the Demo

For native simulation:
```bash
./build/zephyr/zephyr.exe
```

For hardware, connect to serial console to see output:
```bash
screen /dev/ttyACM0 115200
# or
minicom -D /dev/ttyACM0
```

### Step 3: Monitor on Central System

1. Open your browser to https://cs.ocpp-css.com/
2. Look for "ZephyrCharger_v2" in the connected charge points
3. Monitor the charging sessions and meter values in real-time!

## What You'll See

The demo runs automatically through this scenario:

```
T=0s    : Initializing network and OCPP stack
T=5s    : Vehicle 1 arrives (Fast charging - 22kW)
T=8s    : Vehicle 2 arrives (Normal charging - 7.4kW)
T=11s   : Both vehicles start charging
T=11-71s: Active charging with live meter updates
          📊 Status updates every 5 seconds showing:
          - Power delivery (kW)
          - Current draw (A)
          - Energy delivered (Wh)
          - Battery State of Charge (%)
          - Connector temperature (°C)
T=71s   : Charging completes (95% SoC reached)
T>71s   : Station ready for remote operations
```

## Expected Console Output

You'll see output like this:

```
╔═══════════════════════════════════════════════════════╗
║   OCPP EV Charging Station Demo                      ║
║   Platform: native_sim                                ║
║   Connectors: 2 (Fast + Normal)                      ║
╚═══════════════════════════════════════════════════════╝

[00:00:02.642] <inf> main: ✅ Network connected!
[00:00:03.201] <inf> main: ✅ OCPP stack initialized!
[00:00:08.500] <inf> main: ⚡ CHARGING STARTED on connector 1 (FAST mode - 22.0 kW)
[00:00:13.500] <inf> main: 📊 Connector 1 Status: 10.5 kW | 45.7 A | 58 Wh | 22% SoC | 29.2°C | 5s
[00:00:18.500] <inf> main: 📊 Connector 1 Status: 18.0 kW | 78.3 A | 208 Wh | 25% SoC | 35.8°C | 10s
...
[00:01:03.650] <inf> main: ✅ CHARGING COMPLETED on connector 1
[00:01:03.650] <inf> main:    📈 Final Stats:
[00:01:03.650] <inf> main:       Energy delivered: 1320 Wh
[00:01:03.650] <inf> main:       Charging time: 60 seconds
[00:01:03.650] <inf> main:       Final SoC: 95%
```

## Interactive Features

From the cs.ocpp-css.com web interface, you can:

- **View Real-Time Status**: See live charging sessions and meter values
- **Remote Start**: Trigger a charging session from the server
- **Remote Stop**: Stop an active charging session
- **View History**: Check transaction logs and historical data

## Troubleshooting

### Network Issues

**Problem**: Can't connect to cs.ocpp-css.com

**Solutions**:
1. Verify your device has internet connectivity
2. Check DNS is working: `ping cs.ocpp-css.com`
3. Ensure firewall allows outbound HTTP on port 80
4. For native_sim, you may need TAP networking configured

### Build Issues

**Problem**: Build fails

**Solutions**:
1. Ensure you have the latest Zephyr SDK
2. Update west: `west update`
3. Clean build: `west build -b <board> samples/net/ocpp_demo -p`

### OCPP Issues

**Problem**: Authorization fails

**Solutions**:
1. Check SNTP time synchronization succeeded
2. Verify server address is correct in prj.conf
3. Enable debug logging: Add `CONFIG_LOG_DEFAULT_LEVEL=4` to prj.conf

## Customization

### Change Server Settings

Edit `samples/net/ocpp_demo/prj.conf`:

```
CONFIG_NET_SAMPLE_OCPP_SERVER="your-server.com"
CONFIG_NET_SAMPLE_OCPP_PORT=8080
```

### Adjust Charging Parameters

Edit `samples/net/ocpp_demo/src/main.c`:

- **Fast charge power**: Change `22.0f` in `init_charging_state()`
- **Normal charge power**: Change `7.4f` in `init_charging_state()`
- **Initial SoC**: Change `soc_percent = 20` in `init_charging_state()`
- **Charging duration**: Adjust `k_sleep()` timers in `main()`

### Add More Connectors

Change `NO_OF_CONN` from 2 to your desired number (update stack arrays accordingly).

## What Makes This Demo "Cool" 😎

1. **Realistic Physics**: Power ramps up gradually, temperature increases realistically
2. **Visual Output**: Emoji indicators make status easy to read at a glance
3. **Dual Speed**: Shows both fast and normal charging simultaneously
4. **Smart Termination**: Automatically stops at 95% like real charging stations
5. **Remote Control**: Full support for remote operations from the server
6. **Comprehensive Metrics**: Reports 6 different meter values (power, current, voltage, energy, temp, SoC)
7. **Production-Ready**: Uses real OCPP 1.6 protocol with a public test server

## Learn More

- Full documentation: `samples/net/ocpp_demo/README.rst`
- Architecture diagram: `samples/net/ocpp_demo/ARCHITECTURE.txt`
- OCPP API docs: Search for "ocpp_interface" in Zephyr docs
- OCPP 1.6 spec: https://www.openchargealliance.org/protocols/ocpp-16/

## Support

If you encounter issues:
1. Check the troubleshooting section above
2. Review the logs with increased verbosity
3. Consult the Zephyr OCPP documentation
4. Check the GitHub repository issues

---

**Enjoy the demo! ⚡🚗🔋**
