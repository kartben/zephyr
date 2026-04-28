# Zephyr RTOS - Simplified Layered Architecture

A simplified, top-down view of the main Zephyr RTOS "layers", from the user's
application down to the hardware. Rendered with Mermaid's
[`block-beta`](https://mermaid.js.org/syntax/block.html) diagram syntax.

```mermaid
block-beta
columns 1

  app["Application"]

  block:subsystems["Subsystems / OS Services"]
    columns 4
    net["Networking<br/>(IP, Sockets, LwM2M)"]
    bt["Bluetooth<br/>(LE, Mesh)"]
    fs["File Systems<br/>(LittleFS, FAT, ext2)"]
    usb["USB<br/>(Host / Device)"]
    shell["Shell"]
    log["Logging"]
    storage["Settings / NVS / Storage"]
    sec["Crypto / TLS"]
    posix["POSIX API"]
    libc["C Library"]
    pm["Power Management"]
    dm["Device Management<br/>(MCUmgr, DFU)"]
  end

  block:kernel["Kernel"]
    columns 4
    sched["Scheduler"]
    thr["Threads"]
    sync["Sync<br/>(sem, mutex)"]
    ipc["IPC<br/>(msgq, mbox, pipe)"]
    mem["Memory Mgmt<br/>(heap, slab)"]
    tmr["Timers / Clock"]
    irq["Interrupts"]
    um["User Mode / MPU<br/>(Memory Domains)"]
  end

  block:hal["HAL / Drivers / Architecture"]
    columns 3
    drv["Device Drivers<br/>(GPIO, I2C, SPI, UART,<br/>Flash, Sensor, Display, ...)"]
    arch["Architecture Support<br/>(ARM, RISC-V, Xtensa,<br/>x86, ARC, ...)"]
    soc["SoC / Board HAL<br/>+ Devicetree"]
  end

  hw["Hardware<br/>(CPU, Memory, Peripherals, Board)"]

  app --> subsystems
  subsystems --> kernel
  kernel --> hal
  hal --> hw

  classDef app fill:#cfe8ff,stroke:#1565c0,color:#0b1f33
  classDef sub fill:#dff5e1,stroke:#2e7d32,color:#0b1f33
  classDef ker fill:#fde9c9,stroke:#ef6c00,color:#0b1f33
  classDef hal fill:#f4d8e8,stroke:#ad1457,color:#0b1f33
  classDef hw  fill:#e0e0e0,stroke:#424242,color:#0b1f33

  class app app
  class subsystems,net,bt,fs,usb,shell,log,storage,sec,posix,libc,pm,dm sub
  class kernel,sched,thr,sync,ipc,mem,tmr,irq,um ker
  class hal,drv,arch,soc hal
  class hw hw
```

## Notes

- **Application** - user code built on top of Zephyr APIs.
- **Subsystems / OS Services** - optional, modular features that an app opts
  into via Kconfig (networking, Bluetooth, file systems, shell, logging,
  POSIX, C library, power management, device management, ...).
- **Kernel** - the small core: scheduler, threads, synchronization, IPC,
  memory, timers, interrupts and user-mode/MPU isolation.
- **HAL / Drivers / Architecture** - the device driver model, per-architecture
  code, SoC/vendor HALs, and the Devicetree-derived hardware description used
  to wire everything to the board.
- **Hardware** - the actual SoC, memory and peripherals on the board.
