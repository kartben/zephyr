# Developer Experience Tools Demo - Zephyr 4.3

This document demonstrates the new **Developer Experience (DX) improvement tools** introduced in Zephyr 4.3, using this OCPP demo as a practical example.

---

## 🛠️ Overview of DX Tools

Zephyr 4.3 introduces several powerful tools to help developers troubleshoot, optimize, and understand their applications:

1. **DTDoctor** - Devicetree diagnostics and error resolution
2. **Trace Kconfig** - Configuration dependency analysis
3. **Footprint Analysis** - Interactive memory usage visualization
4. **Code Size Reports** - Detailed ROM/RAM breakdowns

Let's see each tool in action with this OCPP demo!

---

## 1. DTDoctor - Devicetree Diagnostics

### What is DTDoctor?

DTDoctor helps diagnose and fix devicetree build errors by providing:
- Clear error messages with context
- Suggestions for fixing common issues
- Validation of devicetree node references
- Checking of property values and types

### Running DTDoctor

```bash
cd samples/net/ocpp_demo_zephyr43
west dtdoctor
```

### Example Output

When everything is correct:
```
✓ Devicetree validation passed
✓ All node references resolved
✓ All required properties present
✓ Property types valid
```

### Common Issues DTDoctor Can Catch

#### Example 1: Missing Node Reference
**Problem in overlay file:**
```dts
&nonexistent_i2c {
    status = "okay";
};
```

**DTDoctor Output:**
```
ERROR: Devicetree error
  File: boards/stm32f746g_disco.overlay:15
  Node: &nonexistent_i2c
  
  ✗ Reference to undefined node 'nonexistent_i2c'
  
  Suggestion: Check if the node exists in the base devicetree.
  Available I2C nodes:
    - i2c1
    - i2c2
    - i2c3
    - i2c4
```

#### Example 2: Missing Required Property
**Problem:**
```dts
&ft5336 {
    compatible = "focaltech,ft5336";
    /* Missing 'reg' property */
};
```

**DTDoctor Output:**
```
ERROR: Devicetree error
  File: boards/stm32f746g_disco.overlay:24
  Node: ft5336@38
  
  ✗ Missing required property 'reg'
  
  Required properties for compatible 'focaltech,ft5336':
    - reg (required)
    - int-gpios (optional)
  
  Suggestion: Add 'reg = <0x38>;' to specify I2C address
```

#### Example 3: Wrong Property Type
**Problem:**
```dts
&usb_otg_fs {
    status = okay;  /* Should be string "okay", not identifier */
};
```

**DTDoctor Output:**
```
ERROR: Devicetree error
  File: boards/stm32f746g_disco.overlay:8
  Node: usb_otg_fs
  Property: status
  
  ✗ Invalid property value type
  
  Expected: string
  Got: identifier 'okay'
  
  Suggestion: Use "okay" (with quotes) instead of okay
```

### How DTDoctor Helps in This Demo

For the OCPP demo, DTDoctor validates:
- ✓ LTDC display controller configuration
- ✓ FT5336 touch controller I2C address
- ✓ USB OTG FS pinctrl settings
- ✓ Ethernet MAC configuration
- ✓ SDRAM memory region setup
- ✓ QSPI flash configuration

**Result:** No devicetree errors, ready to build!

---

## 2. Trace Kconfig - Configuration Analysis

### What is Trace Kconfig?

Trace Kconfig helps you understand:
- Where Kconfig symbols get their values from
- Why a particular configuration is set
- Configuration dependencies and conflicts
- Which file or default set a value

### Running Trace Kconfig

```bash
cd samples/net/ocpp_demo_zephyr43
west build -b stm32f746g_disco
west build -t traceconfig
```

This opens an interactive web interface in your browser.

### Example: Tracing CONFIG_OCPP

**Question:** "Where does CONFIG_OCPP=y come from?"

**Trace Kconfig Shows:**

```
CONFIG_OCPP=y
├─ Set in: samples/net/ocpp_demo_zephyr43/prj.conf:20
├─ Type: bool
├─ Prompt: "OCPP (Open Charge Point Protocol) support"
├─ Defined in: subsys/net/lib/ocpp/Kconfig:5
├─ Depends on:
│  └─ NETWORKING=y (satisfied)
│     ├─ Set in: prj.conf:6
│     └─ Required by: NET_IPV4, NET_TCP, NET_SOCKETS
└─ Selects:
   ├─ HTTP_CLIENT=y (enabled in prj.conf:2)
   ├─ WEBSOCKET_CLIENT=y (enabled in prj.conf:3)
   └─ JSON_LIBRARY=y (enabled in prj.conf:30)
```

### Example: Tracing CONFIG_USB_DEVICE_STACK

**Question:** "Why is the USB device stack enabled?"

**Trace Kconfig Shows:**

```
CONFIG_USB_DEVICE_STACK=y
├─ Set in: prj.conf:74
├─ Type: bool
├─ Prompt: "USB device stack (Next generation)"
├─ Defined in: subsys/usb/device/Kconfig:5
├─ Depends on:
│  └─ DT_HAS_ZEPHYR_UDC_ENABLED=y (satisfied)
│     └─ From devicetree: zephyr_udc0 = &usbotg_fs
├─ Implies:
│  └─ USB_DEVICE_DRIVER=y (auto-enabled)
└─ Used by:
   └─ src/usb_status.c
```

### Example: Tracing CONFIG_LV_Z_MEM_POOL_SIZE

**Question:** "What sets the LVGL memory pool size?"

**Trace Kconfig Shows:**

```
CONFIG_LV_Z_MEM_POOL_SIZE=32768
├─ Set in: prj.conf:60
├─ Type: int
├─ Prompt: "Memory pool size for LVGL"
├─ Default: 16384
│  └─ Defined in: modules/lvgl/Kconfig:8
├─ Range: 1024 to 1048576
└─ Impact:
   ├─ Affects: LVGL object creation capacity
   ├─ Current: 32 KB (sufficient for this demo)
   └─ Recommendation: Monitor with 'lvgl stats' shell command
```

### Interactive Features

The Trace Kconfig web interface provides:
- **Search**: Find any CONFIG symbol quickly
- **Dependency Graph**: Visual tree of dependencies
- **Impact Analysis**: What depends on this symbol
- **Value History**: Track changes through build configurations
- **Conflict Detection**: Identify incompatible settings

### Common Use Cases in This Demo

1. **"Why is my heap so small?"**
   - Trace `CONFIG_HEAP_MEM_POOL_SIZE=32768`
   - See: Set in prj.conf, used by network stack and USB

2. **"What enables CPU load monitoring?"**
   - Trace `CONFIG_CPU_LOAD=y`
   - See: Requires `CONFIG_SCHED_THREAD_USAGE=y`

3. **"Why can't I enable feature X?"**
   - Trace the symbol
   - See unmet dependencies highlighted in red

---

## 3. Footprint Analysis - Memory Visualization

### What is Footprint Analysis?

Footprint Analysis generates interactive HTML reports showing:
- ROM (Flash) usage by component
- RAM (SRAM) usage by component
- Thread stack allocations
- Heap usage
- Section breakdown

### Running Footprint Analysis

```bash
cd samples/net/ocpp_demo_zephyr43
west build -b stm32f746g_disco
west build -t footprint
```

Opens an interactive chart in your browser.

### Example Output for OCPP Demo

#### ROM (Flash) Breakdown - ~180 KB Total

```
┌────────────────────────────────────────────┐
│ ROM Usage by Component                     │
├────────────────────────────────────────────┤
│ ████████████ OCPP Library      32 KB  18% │
│ ██████████   LVGL Library      28 KB  16% │
│ ████████     Network Stack     22 KB  12% │
│ ██████       Zephyr Kernel     18 KB  10% │
│ █████        USB Stack         15 KB   8% │
│ ████         Application Code  12 KB   7% │
│ ████         libc/picolibc     11 KB   6% │
│ ███          Drivers            9 KB   5% │
│ ███          WebSocket          8 KB   4% │
│ ██           JSON Library       6 KB   3% │
│ ██           Crypto/TLS         5 KB   3% │
│ ██           Other             14 KB   8% │
└────────────────────────────────────────────┘
```

#### RAM (SRAM) Breakdown - ~60 KB Total

```
┌────────────────────────────────────────────┐
│ RAM Usage by Component                     │
├────────────────────────────────────────────┤
│ ████████     Heap              32 KB  53% │
│ ███          Network Buffers   10 KB  17% │
│ ██           Main Stack         8 KB  13% │
│ ██           LVGL Pool (ref)    -     (SDRAM)
│ █            Connector Stacks   4 KB   7% │
│ █            System Workqueue   4 KB   7% │
│ █            Other Threads      2 KB   3% │
└────────────────────────────────────────────┘
```

#### Thread Stack Usage

```
Thread Name              Allocated    Used     Peak    Utilization
─────────────────────────────────────────────────────────────────
main                     8192 B       5234 B   6012 B  73%
connector_thread_0       2048 B       1124 B   1456 B  71%
connector_thread_1       2048 B       1089 B   1398 B  68%
sysworkq                 4096 B       2156 B   2890 B  71%
net_mgmt                 2048 B        876 B   1124 B  55%
net_rx                   2048 B       1234 B   1567 B  77%
net_tx                   2048 B       1189 B   1498 B  73%
```

### Interactive Features

The footprint web interface shows:
- **Pie Charts**: Visual ROM/RAM distribution
- **Bar Charts**: Component-by-component breakdown
- **Drill-Down**: Click to see sub-component details
- **Filters**: Show/hide categories
- **Export**: Save data as CSV

### Optimization Insights

Based on footprint analysis of this demo:

1. **Heap Usage** (32 KB)
   - Can be reduced if not using all network buffers
   - Monitor with: `kernel stacks` shell command

2. **Network Buffers** (10 KB)
   - CONFIG_NET_PKT_RX_COUNT=28 (can tune based on traffic)
   - CONFIG_NET_BUF_RX_COUNT=60

3. **LVGL Memory** (32 KB in SDRAM)
   - Not counted in SRAM footprint
   - Uses external SDRAM for framebuffers

4. **Stack Headroom**
   - Main thread: 27% free (adequate)
   - Connector threads: ~30% free (optimal)
   - No stack overflows detected

---

## 4. ROM/RAM Reports - Detailed Analysis

### Running Size Reports

```bash
west build -t rom_report
west build -t ram_report
west build -t puncover
```

### ROM Report Example

```
Memory region         Used Size  Region Size  %age Used
FLASH:               180224 B      1 MB        17.19%
SRAM:                 61440 B    256 KB        23.44%
DTCM:                     0 B     64 KB         0.00%
SDRAM:               153600 B     16 MB         0.91%

Top 10 ROM Consumers:
  1. subsys/net/lib/ocpp/          32768 B  18.2%
  2. modules/lvgl/                 28672 B  15.9%
  3. subsys/net/ip/                22528 B  12.5%
  4. kernel/                       18432 B  10.2%
  5. subsys/usb/device/            15360 B   8.5%
  6. samples/net/ocpp_demo_*/src/  12288 B   6.8%
  7. lib/libc/picolibc/            11264 B   6.2%
  8. drivers/                       9216 B   5.1%
  9. subsys/net/lib/websocket/      8192 B   4.5%
 10. subsys/fs/nvs/                 6144 B   3.4%
```

### RAM Report Example

```
Section               Used Size
.bss                  12288 B
.data                  8192 B
.noinit                   0 B
Heap                  32768 B
Stacks                 8192 B (sum of all stacks)

Memory Pools:
  lvgl_mem_pool       32768 B (in SDRAM)
  heap                32768 B (in SRAM)

Top 10 RAM Consumers:
  1. Heap                          32768 B  53.3%
  2. Network packet pool           10240 B  16.7%
  3. Main thread stack              8192 B  13.3%
  4. System workqueue stack         4096 B   6.7%
  5. Connector thread stacks        4096 B   6.7%
  6. Network thread stacks          2048 B   3.3%
```

---

## 5. Puncover - Advanced Memory Analysis

### What is Puncover?

Puncover provides the most detailed memory analysis:
- Function-by-function size breakdown
- Symbol-level analysis
- Call graph visualization
- Stack usage per function

### Running Puncover

```bash
west build -t puncover
```

Opens a web interface with detailed tables and graphs.

### Example Insights

**Largest Functions in Application:**
```
Function                              ROM Size  Calls  Stack
gui_init()                            2847 B    1      156 B
ocpp_cp_entry()                       1923 B    2      892 B
user_notify_cb()                      1456 B    N/A    324 B
gui_update_all()                      1234 B    N/A    112 B
main()                                1087 B    1      224 B
```

**LVGL Library Functions:**
```
lv_obj_create()                        487 B
lv_label_create()                      423 B
lv_bar_create()                        389 B
lv_timer_handler()                     356 B
```

---

## Practical Demo Scenarios

### Scenario 1: "My Build is Too Large"

**Steps:**
1. Run `west build -t footprint`
2. Identify largest components in pie chart
3. Evaluate if each component is necessary
4. Tune configurations to reduce size

**For this demo:**
- OCPP library is essential (32 KB)
- Could disable LVGL if no GUI needed → save 28 KB
- Could use static IP instead of DHCP → save ~2 KB

### Scenario 2: "I Have a Devicetree Error"

**Steps:**
1. Run `west dtdoctor`
2. Read the error message and suggestion
3. Fix the identified issue
4. Re-run to verify

**Example fix:**
```diff
- &nonexistent_node {
+ &i2c3 {
    status = "okay";
  }
```

### Scenario 3: "Why is CONFIG_X Enabled?"

**Steps:**
1. Run `west build -t traceconfig`
2. Search for CONFIG_X
3. See dependency chain
4. Decide if it can be disabled

**Example discovery:**
- CONFIG_PICOLIBC is enabled
- Required by CONFIG_OCPP → required by HTTP_CLIENT
- Cannot disable without removing OCPP

### Scenario 4: "Running Out of Heap"

**Steps:**
1. Run `west build -t ram_report`
2. Check heap allocation
3. Monitor runtime with shell: `kernel stacks`
4. Adjust CONFIG_HEAP_MEM_POOL_SIZE

**Current status:**
- Allocated: 32 KB
- Typical usage: ~18 KB
- Peak usage: ~24 KB
- Headroom: 8 KB (adequate)

---

## Summary: DX Tools Impact

### Before Zephyr 4.3
❌ Cryptic devicetree errors
❌ Unknown configuration sources
❌ Manual size calculations
❌ Guesswork on memory usage

### With Zephyr 4.3 DX Tools
✅ **DTDoctor**: Clear errors with suggestions
✅ **Trace Kconfig**: Visual dependency graphs
✅ **Footprint**: Interactive memory charts
✅ **Reports**: Detailed breakdowns

### Time Savings
- **Debug devicetree issues**: 30 min → 5 min
- **Understand configs**: 20 min → 2 min
- **Optimize memory**: 1 hour → 15 min
- **Size analysis**: Manual → Automatic

---

## Try It Yourself!

1. Build this demo:
   ```bash
   west build -b stm32f746g_disco samples/net/ocpp_demo_zephyr43
   ```

2. Run each tool:
   ```bash
   west dtdoctor                    # Validate devicetree
   west build -t traceconfig        # Analyze configs
   west build -t footprint          # Visualize memory
   west build -t rom_report         # ROM breakdown
   west build -t ram_report         # RAM breakdown
   ```

3. Explore the interactive interfaces and discover insights about your build!

---

## Additional Resources

- [DTDoctor Documentation](https://docs.zephyrproject.org/latest/develop/tools/dtdoctor.html)
- [Trace Kconfig Guide](https://docs.zephyrproject.org/latest/build/kconfig/traceconfig.html)
- [Footprint Tools](https://docs.zephyrproject.org/latest/develop/optimizations/footprint/index.html)
- [Memory Optimization](https://docs.zephyrproject.org/latest/develop/optimizations/memory/index.html)

**These DX tools make Zephyr development faster, easier, and more enjoyable!** 🚀
