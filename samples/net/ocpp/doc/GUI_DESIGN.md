# OCPP LVGL GUI Design Document

## Overview

This document describes the design and implementation of the LVGL-based graphical user interface (GUI) for the Zephyr OCPP (Open Charge Point Protocol) sample application.

## Design Goals

1. **Clear Status Visualization**: Provide at-a-glance understanding of the charging station state
2. **Professional Appearance**: Modern, clean design suitable for an EV charging station
3. **Color Coding**: Use intuitive colors to indicate different states
4. **Real-time Updates**: Display live charging data and status changes
5. **Compact Layout**: Fit essential information in a 480x320 display

## Color Scheme

The GUI uses a modern EV charging station theme inspired by contemporary electric vehicle interfaces:

- **Primary Color (Bright Cyan - #00D9FF)**: Charging indicators, active elements, titles
- **Secondary Color (Deep Blue - #1E3A8A)**: Header background
- **Success Color (Green - #10B981)**: Active/charging status
- **Warning Color (Amber - #F59E0B)**: Authorizing/transitional states
- **Background (Very Dark Blue - #0F172A)**: Main background
- **Cards (Dark Slate - #1E293B)**: Connector card backgrounds

## Layout Structure

### Header (Top)
- Height: 50px
- Contains: "EV CHARGING STATION" title
- Background: Deep blue with rounded corners
- Text: White, bold, 18px

### System Status Bar
- Position: Below header
- Contains: Overall system status message
- Color changes based on state:
  - Green: Connected and operational
  - Amber: Initializing or warnings
  - Red: Error states

### Connector Cards (Main Area)
Two side-by-side cards for Connector 1 and Connector 2:
- Size: 180x140px each
- Features per card:
  - Connector title (cyan, bold)
  - Status indicator (circular LED-style, top right)
  - Status text (current state)
  - Power reading (highlighted in cyan)
  - Energy delivered (cumulative)
  - ID tag (small gray text)

Status indicator colors:
- Gray: Idle
- Amber: Authorizing/Preparing
- Green: Charging

### Network Status Panel (Bottom)
- Size: 380x50px
- Contains:
  - Network status and IP address (left)
  - Current time (right)
- Color coding:
  - Green: Connected
  - Red: Disconnected

### Footer
- Position: Bottom center
- Text: "Zephyr OCPP 1.6" (small, gray)

## State Transitions

The GUI reflects these charging station states:

1. **Idle**: No active charging session
   - Indicator: Gray
   - Power: 0 kW
   - Energy: 0 Wh

2. **Preparing**: Remote start transaction initiated
   - Indicator: Amber
   - Power: 0 kW
   - Status text shows "Preparing"

3. **Authorizing**: Validating user credentials
   - Indicator: Amber
   - Power: 0 kW
   - Status text shows "Authorizing"

4. **Charging**: Active charging session
   - Indicator: Green
   - Power: Shows actual power draw (e.g., 7.20 kW)
   - Energy: Increments with charging progress

5. **Finishing**: Stopping transaction
   - Transitions back to Idle

## Implementation Details

### Files
- `src/ocpp_gui.c`: GUI implementation
- `src/ocpp_gui.h`: GUI API header
- `prj_gui.conf`: LVGL configuration for the build
- `boards/native_sim.overlay`: Display configuration for simulator

### Key Functions

- `ocpp_gui_init()`: Initialize display and create all GUI elements
- `ocpp_gui_update_system_status()`: Update overall system status
- `ocpp_gui_update_network_status()`: Update network connection info
- `ocpp_gui_update_connector()`: Update individual connector state
- `ocpp_gui_update_time()`: Update time display
- `ocpp_gui_task()`: Process LVGL events (call periodically)

### Integration Points

The GUI is integrated into the main OCPP application through conditional compilation:
- Enabled with `CONFIG_LVGL=y`
- Updates triggered from OCPP callback functions
- Runs in main thread, calls `lv_timer_handler()` periodically

## Building and Testing

### Build for Native Simulator
```bash
west build -b native_sim -DCONF_FILE=prj_gui.conf samples/net/ocpp
./build/zephyr/zephyr.exe
```

### Build for Hardware (e.g., STM32)
```bash
west build -b stm32f769i_disco -DCONF_FILE=prj_gui.conf samples/net/ocpp
west flash
```

## Future Enhancements

Potential improvements for future versions:
1. Touch input support for manual connector selection
2. Historical charging graphs
3. Multi-language support
4. Additional status indicators (temperature, current, voltage)
5. QR code display for payment/authentication
6. Animated transitions between states
7. Energy cost calculation and display
