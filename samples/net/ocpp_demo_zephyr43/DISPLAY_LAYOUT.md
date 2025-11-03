# Display Layout - Zephyr 4.3 OCPP Demo

## Screen Layout (480x272 pixels)

This document describes the visual layout of the LVGL-based GUI for the OCPP demo.

### Full Screen Layout

```
┌────────────────────────────────────────────────────────────────────┐
│                     Zephyr 4.3 OCPP Demo                    (Title)│  ← White, 20pt
├────────────────────────────────────────────────────────────────────┤
│ NET: 192.168.1.101 │ OCPP: Online │ USB: Yes │ Up: 01h23m45s      │  ← Status Row
│                                                                    │
│ CPU: ████████████████████░░░░░░░ 65%          216MHz              │  ← CPU Bar
│                                                                    │
├───────────────────────────────┬────────────────────────────────────┤
│                               │                                    │
│      ╔═══════════════╗        │        ╔═══════════════╗          │
│      ║  Connector 1  ║        │        ║  Connector 2  ║          │
│      ╠═══════════════╣        │        ╠═══════════════╣          │
│      ║               ║        │        ║               ║          │
│      ║ State:        ║        │        ║ State:        ║          │
│      ║   Charging    ║        │        ║   Available   ║          │
│      ║               ║        │        ║               ║          │
│      ║ Meter: 125 Wh ║        │        ║ Meter: 0 Wh   ║          │
│      ║ ID: ZepId00   ║        │        ║ ID: --        ║          │
│      ║ Txn: 12345    ║        │        ║ Txn: --       ║          │
│      ║               ║        │        ║               ║          │
│      ╚═══════════════╝        │        ╚═══════════════╝          │
│                               │                                    │
└───────────────────────────────┴────────────────────────────────────┘
```

## Color Scheme

### Background
- Main screen: Black (#000000)
- Panels: Dark gray (#2a2a2a)
- CPU panel: Very dark gray (#1a1a1a)

### Text Colors
- Title: White (#FFFFFF)
- Status indicators (connected): Green (#00FF00)
- Status indicators (disconnected): Red (#FF0000)
- USB indicator (not connected): Orange (#FF6600)
- Generic info: Light gray (#CCCCCC)

### Connector State Colors
- **Available**: Green border
- **Preparing**: Yellow border
- **Charging**: Blue border
- **Finishing**: Orange border
- **Faulted**: Red border

### CPU Load Bar Colors
- 0-50%: Green (#00FF00)
- 50-80%: Yellow (#FFFF00)
- 80-100%: Red (#FF0000)

## Detailed Component Breakdown

### 1. Title Bar (Y: 0-35)
```
┌────────────────────────────────────────────────────┐
│         Zephyr 4.3 OCPP Demo                       │
└────────────────────────────────────────────────────┘
```
- Font: Montserrat 20
- Color: White
- Alignment: Center
- Position: Top, 5px margin

### 2. Status Row (Y: 35-55)
```
┌──────────────┬─────────────┬──────────┬─────────────┐
│ NET: IP/Disc │ OCPP: State │ USB: Y/N │ Up: Time    │
└──────────────┴─────────────┴──────────┴─────────────┘
```
- Font: Montserrat 12
- Updates: Every 500ms
- Network: Shows IP when connected, "Disconnected" otherwise
- OCPP: "Online" (green) or "Offline" (red)
- USB: "Yes" (green) or "No" (orange)
- Uptime: HH:MM:SS format

### 3. CPU Panel (Y: 55-95)
```
┌─────────────────────────────────────────────────────┐
│ CPU: [████████████░░░░░░] 65%    216MHz             │
└─────────────────────────────────────────────────────┘
```
- Font: Montserrat 12-14
- Bar width: 280px
- Bar range: 0-100%
- Update rate: 500ms
- Color changes based on load

### 4. Connector Panels (Y: 100-260)

#### Left Panel (Connector 1)
```
┌───────────────────────┐
│    Connector 1        │  ← Montserrat 16, White
├───────────────────────┤
│ State: Charging       │  ← Montserrat 14, State Color
│                       │
│ Meter: 125 Wh         │  ← Montserrat 12, Light Gray
│ ID: ZepId00           │
│ Txn: 12345            │
└───────────────────────┘
```
- Position: X: 10, Y: 100
- Size: 225 x 160
- Border: 2px, color matches state
- Background: #2a2a2a

#### Right Panel (Connector 2)
```
┌───────────────────────┐
│    Connector 2        │
├───────────────────────┤
│ State: Available      │
│                       │
│ Meter: 0 Wh           │
│ ID: --                │
│ Txn: --               │
└───────────────────────┘
```
- Position: X: 245, Y: 100
- Size: 225 x 160
- Border: 2px, color matches state
- Background: #2a2a2a

## State Transitions Visualized

### Connector State: Available → Charging

**Step 1: Available**
```
┌───────────────────────┐
│    Connector 1        │
│ State: Available      │ ← Green
│ Meter: 0 Wh           │
│ ID: --                │
│ Txn: --               │
└───────────────────────┘
Border: Green
```

**Step 2: Preparing**
```
┌───────────────────────┐
│    Connector 1        │
│ State: Preparing      │ ← Yellow
│ Meter: 0 Wh           │
│ ID: ZepId00           │
│ Txn: --               │
└───────────────────────┘
Border: Yellow
```

**Step 3: Charging**
```
┌───────────────────────┐
│    Connector 1        │
│ State: Charging       │ ← Blue
│ Meter: 45 Wh          │ ← Incrementing
│ ID: ZepId00           │
│ Txn: 67890            │ ← Transaction ID
└───────────────────────┘
Border: Blue
```

**Step 4: Finishing**
```
┌───────────────────────┐
│    Connector 1        │
│ State: Finishing      │ ← Orange
│ Meter: 125 Wh         │ ← Final value
│ ID: ZepId00           │
│ Txn: 67890            │
└───────────────────────┘
Border: Orange
```

**Step 5: Back to Available**
```
┌───────────────────────┐
│    Connector 1        │
│ State: Available      │ ← Green
│ Meter: 0 Wh           │ ← Reset
│ ID: --                │
│ Txn: --               │
└───────────────────────┘
Border: Green
```

## Animation and Updates

### Update Frequencies

| Element              | Update Rate | Notes                        |
|---------------------|-------------|------------------------------|
| Network Status      | 500ms       | Static once connected        |
| OCPP Status         | 500ms       | Changes on connect/disconnect|
| USB Status          | 500ms       | Changes on plug/unplug       |
| Uptime              | 500ms       | Continuously incrementing    |
| CPU Load Bar        | 500ms       | Animated with LV_ANIM_ON     |
| CPU Load %          | 500ms       | Number updates               |
| Connector State     | On change   | Event-driven                 |
| Meter Values        | ~1s         | During charging              |
| Transaction ID      | On change   | Event-driven                 |

### Smooth Transitions

- CPU load bar uses LVGL animation: `lv_bar_set_value(bar, value, LV_ANIM_ON)`
- State color changes are instant for clarity
- Meter values update without animation (direct number change)

## Touch Interaction (Future Enhancement)

While the current version primarily displays information, the touchscreen is ready for future interactive features:

### Potential Touch Targets

```
┌─────────────────────────────────────────┐
│  [Title] ← Info/Settings modal          │
├─────────────────────────────────────────┤
│  [Status Row] ← Network details         │
│  [CPU Panel] ← CPU stats modal          │
├──────────────────┬──────────────────────┤
│ [Connector 1]    │ [Connector 2]        │ ← Connector details
│  ┌──────────┐    │  ┌──────────┐        │
│  │  START   │    │  │  START   │        │ ← Manual start
│  └──────────┘    │  └──────────┘        │
│  ┌──────────┐    │  ┌──────────┐        │
│  │   STOP   │    │  │   STOP   │        │ ← Manual stop
│  └──────────┘    │  └──────────┘        │
└──────────────────┴──────────────────────┘
```

## Example Screenshots Descriptions

### Scenario 1: System Idle
- Both connectors available (green border)
- CPU load at ~10% (green bar)
- Network connected
- OCPP online
- USB connected
- Uptime showing

### Scenario 2: One Connector Charging
- Connector 1: Charging (blue border), meter incrementing
- Connector 2: Available (green border)
- CPU load at ~40% (green bar)
- All systems online

### Scenario 3: Both Connectors Active
- Connector 1: Charging (blue border), meter at 85 Wh
- Connector 2: Charging (blue border), meter at 42 Wh
- CPU load at ~65% (yellow bar)
- Increased system activity

### Scenario 4: Error State
- Connector 1: Faulted (red border)
- Connector 2: Available (green border)
- OCPP: Offline (red text)
- Network: Connected
- System attempting recovery

## Accessibility Considerations

### Color Choices
- High contrast ratios (white on black, colored on dark gray)
- Multiple indicators for each state (color + text)
- Large touch targets (prepared for future)

### Font Sizes
- Title: 20pt (clearly readable)
- Headings: 16pt
- Primary info: 14pt
- Secondary info: 12pt

### Visual Hierarchy
1. Title (most prominent)
2. Status indicators (important)
3. CPU load (visible but not distracting)
4. Connector panels (main content)
5. Detailed metrics (supporting info)

## Display Hardware Specs

- LCD: 480x272 RGB565
- Touch: Capacitive, FT5336 controller
- Backlight: GPIO controlled
- Update rate: ~30 FPS (LVGL managed)
- Color depth: 16-bit (65,536 colors)

## Tips for Screenshots/Videos

When documenting this demo:

1. **Power-on sequence**: Show boot messages → GUI initialization
2. **Network connection**: Capture DHCP → IP assignment → OCPP connect
3. **Start charging**: SteVe command → Preparing → Charging transition
4. **Real-time updates**: Show meter values incrementing, CPU load changing
5. **Stop charging**: Stop command → Finishing → Available transition
6. **USB plug**: Show USB status change from No → Yes
7. **Touch interaction**: (Future) demonstrate touch responsiveness

This provides a complete picture of the Zephyr 4.3 capabilities in action!
