# Zephyr 4.3 OCPP Demo - Architecture Diagram

## System Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    STM32F746G Discovery Board                           │
│                                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                 │
│  │              │  │              │  │              │                 │
│  │   ARM M7     │  │   Ethernet   │  │     USB      │                 │
│  │   @ 216MHz   │  │   Controller │  │   OTG FS     │                 │
│  │              │  │              │  │              │                 │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘                 │
│         │                 │                  │                          │
│         │                 │                  │                          │
│  ┌──────▼────────────────────────────────────▼───────┐                 │
│  │                                                    │                 │
│  │             Zephyr RTOS Kernel                    │                 │
│  │                                                    │                 │
│  └────────────────────────┬───────────────────────────┘                 │
│                           │                                             │
│  ┌────────────────────────▼────────────────────────────┐               │
│  │                   Subsystems                        │               │
│  ├─────────────┬────────────┬────────────┬────────────┤               │
│  │   Network   │   USB      │  CPU Load  │  Display   │               │
│  │   Stack     │  Device    │  Monitor   │  (LTDC)    │               │
│  └─────┬───────┴─────┬──────┴──────┬─────┴──────┬─────┘               │
│        │             │             │            │                      │
│  ┌─────▼──────┬──────▼─────┬───────▼────┬───────▼─────┐               │
│  │            │            │            │             │               │
│  │   OCPP     │    USB     │    CPU     │    LVGL     │               │
│  │  Library   │  Status    │  Monitor   │     GUI     │               │
│  │            │            │            │             │               │
│  └─────┬──────┴──────┬─────┴──────┬─────┴──────┬──────┘               │
│        │             │            │            │                      │
│        └─────────────┴────────────┴────────────┘                      │
│                              │                                         │
│                      ┌───────▼────────┐                                │
│                      │                │                                │
│                      │  Main App      │                                │
│                      │  Integration   │                                │
│                      │                │                                │
│                      └───────┬────────┘                                │
│                              │                                         │
│  ┌───────────────────────────▼──────────────────────────┐             │
│  │              Display & Touch Interface                │             │
│  │            480x272 LCD + FT5336 Touch                 │             │
│  └────────────────────────────────────────────────────────┘             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
         │                              │                      │
         ▼                              ▼                      ▼
  ┌──────────────┐            ┌──────────────┐       ┌──────────────┐
  │   Central    │            │     USB      │       │   Network    │
  │   System     │            │     Host     │       │   Router     │
  │   (SteVe)    │            │              │       │   (DHCP)     │
  └──────────────┘            └──────────────┘       └──────────────┘
   WebSocket/OCPP              USB Device Next         Ethernet
```

## Component Interactions

```
┌─────────────┐
│   main.c    │
│             │  Initializes all subsystems
│  ┌────────┐ │  Coordinates OCPP operations
│  │ OCPP   │ │  Manages connector threads
│  │        ├─┼──┐
│  └────────┘ │  │
└──────┬──────┘  │
       │         │
       │         └─────────────────┐
       │                           │
       ▼                           ▼
┌─────────────┐            ┌──────────────┐
│   gui.c     │            │ cpu_monitor  │
│             │            │              │
│ • LVGL UI   │◄───┐       │ • CPU Load   │
│ • Updates   │    │       │ • Freq Info  │
│ • Display   │    │       └──────────────┘
└──────┬──────┘    │
       │           │       ┌──────────────┐
       │           └───────┤ usb_status   │
       │                   │              │
       │                   │ • USB State  │
       │                   │ • Callbacks  │
       │                   └──────────────┘
       │
       ▼
┌──────────────┐
│  Display HW  │
│  (LTDC)      │
│  + Touch     │
│  (FT5336)    │
└──────────────┘
```

## Data Flow

### OCPP Transaction Flow

```
Main Thread              Connector Thread         Central System
    │                          │                        │
    │──── spawn ───────────────>│                        │
    │                          │                        │
    │                          │──── Authorize ────────>│
    │                          │<──── Accepted ─────────│
    │                          │                        │
    │                          │──── StartTxn ─────────>│
    │                          │<──── OK ───────────────│
    │                          │                        │
    │                          │──── MeterValues ──────>│
    │                          │    (periodic)          │
    │                          │                        │
    │──── stop event ──────────>│                        │
    │                          │                        │
    │                          │──── StopTxn ──────────>│
    │                          │<──── OK ───────────────│
    │                          │                        │
    │<──── thread exit ─────────│                        │
```

### GUI Update Flow

```
Timer (500ms)           System State            GUI
    │                        │                    │
    ├──── trigger ───────────>│                    │
    │                        │                    │
    │                        │ Update CPU load    │
    │                        │ Update USB status  │
    │                        │ Update uptime      │
    │                        │                    │
    │                        ├──── refresh ──────>│
    │                        │                    │
    │                        │                    │ Draw CPU bar
    │                        │                    │ Update status
    │                        │                    │ Update connectors
    │                        │                    │
    │                        │<──── done ─────────│
    │                        │                    │
    │◄──── next cycle ────────│                    │
```

## Thread Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                      Thread Overview                         │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐  Priority 0 (Highest)                     │
│  │  Main Thread │  • System initialization                  │
│  │              │  • Network setup                          │
│  └──────────────┘  • OCPP initialization                    │
│                                                              │
│  ┌──────────────┐  Priority 7                               │
│  │ Connector 1  │  • Authorization                          │
│  │   Thread     │  • Transaction management                 │
│  └──────────────┘  • Meter value reporting                  │
│                                                              │
│  ┌──────────────┐  Priority 7                               │
│  │ Connector 2  │  • Authorization                          │
│  │   Thread     │  • Transaction management                 │
│  └──────────────┘  • Meter value reporting                  │
│                                                              │
│  ┌──────────────┐  System workqueue                         │
│  │  GUI Update  │  • Periodic LVGL refresh                  │
│  │    Timer     │  • Display updates                        │
│  └──────────────┘  • Touch handling                         │
│                                                              │
│  ┌──────────────┐  Timer context                            │
│  │  CPU Load    │  • Sample scheduler stats                 │
│  │    Timer     │  • Calculate percentage                   │
│  └──────────────┘  • Update state                           │
│                                                              │
│  ┌──────────────┐  Timer context                            │
│  │   Uptime     │  • Increment counter                      │
│  │    Timer     │  • Update display                         │
│  └──────────────┘                                           │
│                                                              │
│  ┌──────────────┐  Network stack                            │
│  │   Network    │  • Ethernet RX/TX                         │
│  │   Threads    │  • TCP/IP processing                      │
│  └──────────────┘  • WebSocket handling                     │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## Memory Layout

```
┌─────────────────────────────────────────┐
│   Flash (1 MB)                          │
├─────────────────────────────────────────┤
│  • Application code                     │
│  • OCPP library                         │
│  • LVGL library                         │
│  • Network stack                        │
│  • Zephyr kernel                        │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│   SRAM (256 KB)                         │
├─────────────────────────────────────────┤
│  • Kernel stacks                        │
│  • Thread stacks (2x 2KB for connectors)│
│  • Network buffers                      │
│  • Heap (32 KB)                         │
│  • LVGL memory pool (32 KB)             │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│   SDRAM (16 MB)                         │
├─────────────────────────────────────────┤
│  • LVGL framebuffers                    │
│  • Graphics assets                      │
│  • Available for expansion              │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│   QSPI Flash (16 MB)                    │
├─────────────────────────────────────────┤
│  • External code storage (optional)     │
│  • Large assets (optional)              │
│  • Available for expansion              │
└─────────────────────────────────────────┘
```

## Zephyr 4.3 Features Mapping

| Feature                  | Component         | File              |
|--------------------------|-------------------|-------------------|
| OCPP 1.6 Library         | OCPP integration  | main.c            |
| USB Device "Next" Stack  | USB monitoring    | usb_status.c      |
| CPU Load Subsystem       | CPU monitoring    | cpu_monitor.c     |
| CPU Freq (display only)  | CPU monitoring    | cpu_monitor.c     |
| LVGL GUI                 | Display system    | gui.c             |
| Instrumentation (ready)  | Build config      | prj.conf          |

## Network Communication

```
Board                         Network                    Central System
  │                              │                             │
  │──── DHCP Discover ──────────>│                             │
  │<─── DHCP Offer ──────────────│                             │
  │──── DHCP Request ────────────>│                             │
  │<─── DHCP Ack ────────────────│                             │
  │                              │                             │
  │──── NTP Query ───────────────┼────────────────────────────>│
  │<─── NTP Response ────────────┼────────────────────────────┤│
  │                              │                             │
  │──── DNS Query (if needed) ───┼────────────────────────────>│
  │<─── DNS Response ────────────┼────────────────────────────┤│
  │                              │                             │
  │──── TCP SYN ─────────────────┼────────────────────────────>│
  │<─── TCP SYN-ACK ─────────────┼────────────────────────────┤│
  │──── TCP ACK ─────────────────┼────────────────────────────>│
  │                              │                             │
  │──── WebSocket Upgrade ───────┼────────────────────────────>│
  │<─── Upgrade OK ──────────────┼────────────────────────────┤│
  │                              │                             │
  │──── OCPP BootNotification ───┼────────────────────────────>│
  │<─── BootNotification Resp ───┼────────────────────────────┤│
  │                              │                             │
  │──── OCPP Messages ───────────┼────────────────────────────>│
  │<─── OCPP Responses ──────────┼────────────────────────────┤│
```

## Peripheral Configuration

```
STM32F746 Peripherals Used:
├── RCC: Clock configuration (216 MHz)
├── LTDC: Display controller
├── FMC: External SDRAM controller
├── QUADSPI: External flash controller
├── ETH MAC: Ethernet controller
├── USB OTG FS: USB device controller
├── I2C3: Touch controller (FT5336)
├── USART1: Debug console
└── GPIOs: Various control signals
```
