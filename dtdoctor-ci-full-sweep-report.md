# dtdoctor CI Full Sweep Report

Weekly-CI twister report (kartben/zephyr) — every distinct devicetree-related
build failure attempted through dtdoctor's `-DZEPHYR_SCA_VARIANT=dtdoctor` SCA
variant, built for real against Zephyr SDK v1.0.1, real vendor HALs, and a
real west workspace.

Case 0 — `tests/drivers/coredump/coredump_api` on
`qemu_riscv32/qemu_virt_riscv32/aia-direct` — is covered separately in
[`dtdoctor-ci-demo-report.md`](./dtdoctor-ci-demo-report.md), and since fixed
in PR #502. This report documents the 19 additional items attempted in this
sweep and does not repeat that write-up.

## 1. Summary

- **Total attempted (this sweep):** 19
- **Matched** (dtdoctor successfully diagnosed a real CI failure end-to-end): **18**
- **Not-a-dt-issue:** **1**
  - `heltec_t114_v2` — plain C bug (undeclared local variable `drv_data` in `drivers/led_strip/ws2812_gpio.c`), not a leaked devicetree macro; no dtdoctor build was run for it.
- **Blocked:** **0** — every item attempted this sweep reached either a `matched` or `not-a-dt-issue` verdict; none were abandoned for infra/module/config reasons.

## 2. Matched cases

### kit_t2g_b_h_evk

- **Board:** `kit_t2g_b_h_evk/cyt4bfbche/m0p`
- **Test:** `tests/subsys/mgmt/mcumgr/fs_mgmt_hash_supported` (`mgmt.mcumgr.fs.mgmt.hash.supported.all`, `EXTRA_CONF_FILE="configuration/all.conf"`)

Baseline error:

```
FAILED: zephyr/drivers/flash/CMakeFiles/drivers__flash.dir/flash_infineon.c.obj
... (arm-zephyr-eabi-gcc invocation omitted) ...
In file included from /home/user/zephyr/include/zephyr/arch/arm/arch.h:20,
                 from /home/user/zephyr/include/zephyr/arch/cpu.h:19,
                 from /home/user/zephyr/include/zephyr/kernel_includes.h:36,
                 from /home/user/zephyr/include/zephyr/kernel.h:17,
                 from /home/user/zephyr/drivers/flash/flash_priv.h:11,
                 from /home/user/zephyr/drivers/flash/flash_infineon.c:10:
/tmp/build-kit_t2g_b_h_evk-base/zephyr/include/generated/zephyr/devicetree_generated.h:4442:75: error: 'DT_N_S_flash_controller_40240000_S_flash_10000000' undeclared here (not in a function); did you mean 'DT_N_S_flash_controller_40240000_S_flash_10000000_ORD'?
 4442 | #define DT_N_S_flash_controller_40240000_FOREACH_CHILD_STATUS_OKAY(fn) fn(DT_N_S_flash_controller_40240000_S_flash_10000000) fn(DT_N_S_flash_controller_40240000_S_flash_10080000)
      |                                                                           ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:6282:29: note: in definition of macro 'DT_CAT3'
 6282 | #define DT_CAT3(a1, a2, a3) a1 ## a2 ## a3
      |                             ^~
/home/user/zephyr/drivers/flash/flash_infineon.c:38:29: note: in expansion of macro 'DT_PROP'
   38 |         .write_block_size = DT_PROP(SOC_NV_FLASH_NODE, write_block_size),
      |                             ^~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:69:53: note: in expansion of macro '__DEBRACKET'
   69 | #define __GET_ARG2_DEBRACKET(ignore_this, val, ...) __DEBRACKET val
      |                                                     ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:64:9: note: in expansion of macro '__GET_ARG2_DEBRACKET'
   64 |         __GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)
      |         ^~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:59:9: note: in expansion of macro '__COND_CODE'
   59 |         __COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_macro.h:210:9: note: in expansion of macro 'Z_COND_CODE_1'
  210 |         Z_COND_CODE_1(_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~~~
/home/user/zephyr/drivers/flash/flash_priv.h:21:9: note: in expansion of macro 'COND_CODE_1'
   21 |         COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, soc_nv_flash), (node_id), ())
      |         ^~~~~~~~~~~
/tmp/build-kit_t2g_b_h_evk-base/zephyr/include/generated/zephyr/devicetree_generated.h:4442:72: note: in expansion of macro 'SOC_NV_FLASH_COMPAT_NODE'
 4442 | #define DT_N_S_flash_controller_40240000_FOREACH_CHILD_STATUS_OKAY(fn) fn(DT_N_S_flash_controller_40240000_S_flash_10000000) fn(DT_N_S_flash_controller_40240000_S_flash_10080000)
      |                                                                        ^~
/home/user/zephyr/include/zephyr/devicetree.h:6280:24: note: in expansion of macro 'DT_N_S_flash_controller_40240000_FOREACH_CHILD_STATUS_OKAY'
 6280 | #define DT_CAT(a1, a2) a1 ## a2
      |                        ^~
/home/user/zephyr/include/zephyr/devicetree.h:3801:9: note: in expansion of macro 'DT_CAT'
 3801 |         DT_CAT(node_id, _FOREACH_CHILD_STATUS_OKAY)(fn)
      |         ^~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:4832:9: note: in expansion of macro 'DT_FOREACH_CHILD_STATUS_OKAY'
 4832 |         DT_FOREACH_CHILD_STATUS_OKAY(DT_DRV_INST(inst), fn)
      |         ^~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:146:36: note: in expansion of macro 'DT_N_INST_0_infineon_flash_controller'
  146 | #define UTIL_PRIMITIVE_CAT(a, ...) a##__VA_ARGS__
      |                                    ^
/home/user/zephyr/include/zephyr/sys/util_internal.h:145:26: note: in expansion of macro 'UTIL_PRIMITIVE_CAT'
  145 | #define UTIL_CAT(a, ...) UTIL_PRIMITIVE_CAT(a, __VA_ARGS__)
      |                          ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:401:31: note: in expansion of macro 'UTIL_CAT'
  401 | #define DT_INST(inst, compat) UTIL_CAT(DT_N_INST, DT_DASH(inst, compat))
      |                               ^~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:4630:27: note: in expansion of macro 'DT_INST'
 4630 | #define DT_DRV_INST(inst) DT_INST(inst, DT_DRV_COMPAT)
      |                           ^~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:4832:38: note: in expansion of macro 'DT_DRV_INST'
 4832 |         DT_FOREACH_CHILD_STATUS_OKAY(DT_DRV_INST(inst), fn)
      |                                      ^~~~~~~~~~~
/home/user/zephyr/drivers/flash/flash_priv.h:29:9: note: in expansion of macro 'DT_INST_FOREACH_CHILD_STATUS_OKAY'
   29 |         DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, SOC_NV_FLASH_COMPAT_NODE)
      |         ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/drivers/flash/flash_infineon.c:12:27: note: in expansion of macro 'SOC_NV_FLASH_CHILD_NODE'
   12 | #define SOC_NV_FLASH_NODE SOC_NV_FLASH_CHILD_NODE(0)
      |                           ^~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/drivers/flash/flash_infineon.c:38:37: note: in expansion of macro 'SOC_NV_FLASH_NODE'
   38 |         .write_block_size = DT_PROP(SOC_NV_FLASH_NODE, write_block_size),
      |                                     ^~~~~~~~~~~~~~~~~
In file included from /home/user/zephyr/include/zephyr/devicetree.h:20:
/tmp/build-kit_t2g_b_h_evk-base/zephyr/include/generated/zephyr/devicetree_generated.h:148:24: error: expected '}' before numeric constant
(...many further cascading "expected ')'/'}'/';' before numeric constant" errors through devicetree_generated.h expansions in flash_infineon.c...)
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-kit_t2g_b_h_evk-dtd
```

dtdoctor diagnosis:

```
+--------------------------------------------------------------------------------------------------------------+
| DT Doctor                                                                                                    |
+==============================================================================================================+
| 'flash0: /flash-controller@40240000/flash@10000000' is enabled but no driver appears to be available for it. |
+--------------------------------------------------------------------------------------------------------------+
```

dtdoctor correctly identified the "enabled node, no bound driver" family: the
`flash@10000000` child node of the flash controller is `okay` in devicetree
but nothing implements its compatible, and the leaked `DT_N_S_..._S_flash_10000000`
token-paste macro name traces directly back to that node.

### beaglebadge

- **Board:** `beaglebadge/am62l3/a53`
- **Test:** `tests/kernel/device` (`kernel.device`)

Baseline error:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/aarch64-zephyr-elf/bin/../lib/gcc/aarch64-zephyr-elf/14.3.0/../../../../aarch64-zephyr-elf/bin/ld.bfd: zephyr/drivers/serial/libdrivers__serial.a(uart_ns16550.c.obj):(.data.__pm_device_dts_ord_59+0x20): undefined reference to `__device_dts_ord_58'
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-beaglebadge-base
```

dtdoctor diagnosis:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/aarch64-zephyr-elf/bin/../lib/gcc/aarch64-zephyr-elf/14.3.0/../../../../aarch64-zephyr-elf/bin/ld.bfd: zephyr/drivers/serial/libdrivers__serial.a(uart_ns16550.c.obj):(.data.__pm_device_dts_ord_59+0x20): undefined reference to `__device_dts_ord_58'
collect2: error: ld returned 1 exit status
+----------------------------------------------------------------------------------------------------------+
| DT Doctor                                                                                                |
+==========================================================================================================+
| 'main_uart0_pd: /power-domains/power-domain@59' is enabled but no driver appears to be available for it. |
|                                                                                                          |
| Try enabling these Kconfig options:                                                                      |
|                                                                                                          |
|  - CONFIG_ARM_SCMI_POWER_DOMAIN_HELPERS=y                                                                |
|  - CONFIG_POWER_DOMAIN=y                                                                                 |
+----------------------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-beaglebadge-dtd
```

Same "enabled but no driver" family, this time at link time (a `__device_dts_ord_N`
symbol reference with no corresponding `Z_DEVICE_DEFINE`), and dtdoctor
additionally suggested the two Kconfig options that would bind a driver to the
power-domain node.

### ek_ra2a1

- **Board:** `ek_ra2a1/r7fa2a1ab3cfm`
- **Test:** `samples/subsys/usb/midi` (`sample.usb.usb_device_next.midi`)

Baseline error:

```
In file included from /home/user/zephyr/samples/subsys/usb/common/sample_usbd_init.c:10:
/home/user/zephyr/include/zephyr/device.h:96:41: error: '__device_dts_ord_DT_N_NODELABEL_zephyr_udc0_ORD' undeclared here (not in a function)
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                         ^~~~~~~~~
/home/user/zephyr/include/zephyr/usb/usbd.h:527:24: note: in definition of macro 'USBD_DEVICE_DEFINE'
  527 |                 .dev = udc_dev,                                         \
      |                        ^~~~~~~
/home/user/zephyr/include/zephyr/toolchain/common.h:189:23: note: in expansion of macro '_DO_CONCAT'
  189 | #define _CONCAT(x, y) _DO_CONCAT(x, y)
      |                       ^~~~~~~~~~
/home/user/zephyr/include/zephyr/device.h:96:33: note: in expansion of macro '_CONCAT'
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                 ^~~~~~~
/home/user/zephyr/include/zephyr/device.h:300:37: note: in expansion of macro 'DEVICE_DT_NAME_GET'
  300 | #define DEVICE_DT_NAME_GET(node_id) DEVICE_NAME_GET(Z_DEVICE_DT_DEV_ID(node_id))
      |                                     ^~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/device.h:317:34: note: in expansion of macro 'DEVICE_DT_NAME_GET'
  317 | #define DEVICE_DT_GET(node_id) (&DEVICE_DT_NAME_GET(node_id))
      |                                  ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/samples/subsys/usb/common/sample_usbd_init.c:29:20: note: in expansion of macro 'DEVICE_DT_GET'
   29 |                    DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
      |                    ^~~~~~~~~~~~~
[24/206] Building C object zephyr/CMakeFiles/zephyr.dir/lib/utils/hex.c.obj
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-ek_ra2a1-base
```

dtdoctor diagnosis:

```
/home/user/zephyr/samples/subsys/usb/common/sample_usbd_init.c:28:1: error: '__device_dts_ord_DT_N_NODELABEL_zephyr_udc0_ORD' undeclared here (not in a function)
   28 | USBD_DEVICE_DEFINE(sample_usbd,
      | ^~~~~~~~~~~~~~~~~~
+-----------------------------------------------------------------------------------+
| DT Doctor                                                                         |
+===================================================================================+
| No node label 'zephyr_udc0' exists in this build's devicetree.                    |
|                                                                                   |
| Node labels are the 'name:' part written in front of a node in a DTS file, e.g.   |
| 'my_serial: uart@40002000'. In C they are lowercased, so DT_NODELABEL(my_serial). |
|                                                                                   |
| Node labels can be added to an existing node from a devicetree overlay:           |
|                                                                                   |
|     my_serial: &uart0 {};                                                         |
|                                                                                   |
| See <build>/zephyr/zephyr.dts for the devicetree this build actually used.        |
+-----------------------------------------------------------------------------------+
[23/206] Building C object zephyr/CMakeFiles/zephyr.dir/lib/os/assert.c.obj
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-ek_ra2a1-dtd
```

dtdoctor correctly identified the "node-label-does-not-exist" family: `zephyr_udc0`
is not a node label anywhere in this board's devicetree, so `DT_NODELABEL(zephyr_udc0)`
leaked its raw macro name into C.

### scobc_v1

- **Board:** `scobc_v1/versal_rpu`
- **Test:** `tests/lib/c_lib/stdio` (`libraries.libc.common.stdio.minimal`, `CONFIG_MINIMAL_LIBC=y`)

Baseline error:

```
/home/user/zephyr/include/zephyr/device.h:96:41: error: '__device_dts_ord_38' undeclared here (not in a function); did you mean '__device_dts_ord_3'?
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                         ^~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:72:26: note: in definition of macro '__DEBRACKET'
   72 | #define __DEBRACKET(...) __VA_ARGS__
      |                          ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:64:9: note: in expansion of macro '__GET_ARG2_DEBRACKET'
   64 |         __GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)
      |         ^~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:59:9: note: in expansion of macro '__COND_CODE'
   59 |         __COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_macro.h:210:9: note: in expansion of macro 'Z_COND_CODE_1'
  210 |         Z_COND_CODE_1(_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:5966:9: note: in expansion of macro 'COND_CODE_1'
 5966 |         COND_CODE_1(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT),   \
      |         ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/toolchain/common.h:189:23: note: in expansion of macro '_DO_CONCAT'
  189 | #define _DO_CONCAT(x, y) x ## y
      |                       ^~~~~~~~~~
/home/user/zephyr/include/zephyr/device.h:96:33: note: in expansion of macro '_CONCAT'
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                 ^~~~~~~
/home/user/zephyr/include/zephyr/device.h:300:37: note: in expansion of macro 'DEVICE_DT_NAME_GET'
  300 | #define DEVICE_DT_NAME_GET(node_id) DEVICE_NAME_GET(Z_DEVICE_DT_DEV_ID(node_id))
      |                                     ^~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/device.h:317:34: note: in expansion of macro 'DEVICE_DT_GET'
  317 | #define DEVICE_DT_GET(node_id) (&DEVICE_DT_NAME_GET(node_id))
      |                                  ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/drivers/disk/mmc_subsys.c:124:36: note: in expansion of macro 'DISK_ACCESS_MMC_INIT'
  124 |                 .host_controller = DEVICE_DT_GET(DT_INST_PARENT(n)),            \
      |                                    ^~~~~~~~~~~~~
/tmp/build-scobc_v1-base/zephyr/include/generated/zephyr/devicetree_generated.h:6742:50: note: in expansion of macro 'DISK_ACCESS_MMC_INIT'
 6742 | #define DT_FOREACH_OKAY_INST_zephyr_mmc_disk(fn) fn(0)
      |                                                  ^~
/home/user/zephyr/include/zephyr/sys/util_internal.h:146:36: note: in expansion of macro 'UTIL_PRIMITIVE_CAT'
  146 | #define UTIL_PRIMITIVE_CAT(a, ...) a##__VA_ARGS__
      |                                    ^
/home/user/zephyr/drivers/disk/mmc_subsys.c:147:1: note: in expansion of macro 'DT_INST_FOREACH_STATUS_OKAY'
  147 | DT_INST_FOREACH_STATUS_OKAY(DISK_ACCESS_MMC_INIT)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-scobc_v1-base
(Failing file: drivers/disk/mmc_subsys.c, compiled while building test source dir tests/lib/c_lib/stdio for board scobc_v1/versal_rpu with CONFIG_MINIMAL_LIBC=y CONFIG_FILE_SYSTEM=y CONFIG_FAT_FILESYSTEM_ELM=y, which pull in CONFIG_DISK_DRIVER_MMC via CONFIG_FAT_FILESYSTEM_ELM defaults, triggering compilation of drivers/disk/mmc_subsys.c against a devicetree node whose ordinal 38 macro was not generated.)
```

dtdoctor diagnosis:

```
/home/user/zephyr/drivers/disk/mmc_subsys.c:147:1: error: '__device_dts_ord_38' undeclared here (not in a function); did you mean '__device_dts_ord_3'?
  147 | DT_INST_FOREACH_STATUS_OKAY(DISK_ACCESS_MMC_INIT)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
      | __device_dts_ord_3
+------------------------------------------------------------------------------------------------------------+
| DT Doctor                                                                                                  |
+============================================================================================================+
| 'sdhci1: /soc/mmc@f1050000' is disabled in /home/user/zephyr/boards/sc/scobc_v1/scobc_v1_versal_rpu.dts:49 |
| The following nodes depend on it:                                                                          |
|  - /soc/mmc@f1050000/mmc                                                                                   |
|                                                                                                            |
| Try enabling the node by setting its 'status' property to 'okay'.                                          |
+------------------------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-scobc_v1-dtd
```

dtdoctor correctly identified the "disabled ancestor" family, walking from the
compiled-in `mmc` child node up to its disabled `sdhci1` parent and naming the
exact DTS line where the parent's `status` is not `okay`.

### lp_em_cc2340r5

- **Board:** `lp_em_cc2340r5/cc2340r5`
- **Test:** `tests/subsys/mgmt/mcumgr/fs_mgmt_hash_supported` (`mgmt.mcumgr.fs.mgmt.hash.supported.all`, `EXTRA_CONF_FILE="configuration/all.conf"`)

Baseline error:

```
In file included from /home/user/zephyr/include/zephyr/device.h:12,
                 from /home/user/zephyr/drivers/flash/soc_flash_cc23x0.c:8:
/tmp/build-lpem-base/zephyr/include/generated/zephyr/devicetree_generated.h:7705:81: error: 'DT_N_S_soc_S_flash_controller_40021000_S_flash_0' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_flash_controller_40021000_S_flash_0_ORD'?
 7705 | #define DT_N_S_soc_S_flash_controller_40021000_FOREACH_CHILD_STATUS_OKAY(fn) fn(DT_N_S_soc_S_flash_controller_40021000_S_flash_0) fn(DT_N_S_soc_S_flash_controller_40021000_S_flash_4e020000)
      |                                                                                 ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:6282:29: note: in definition of macro 'DT_CAT3'
 6282 | #define DT_CAT3(a1, a2, a3) a1 ## a2 ## a3
      |                             ^~
/home/user/zephyr/drivers/flash/soc_flash_cc23x0.c:25:26: note: in expansion of macro 'DT_PROP'
   25 | #define FLASH_WRITE_SIZE DT_PROP(SOC_NV_FLASH_NODE, write_block_size)
      |                          ^~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:69:53: note: in expansion of macro '__DEBRACKET'
   69 | #define __GET_ARG2_DEBRACKET(ignore_this, val, ...) __DEBRACKET val
      |                                                     ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:64:9: note: in expansion of macro '__GET_ARG2_DEBRACKET'
   64 |         __GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)
      |         ^~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:59:9: note: in expansion of macro '__COND_CODE'
   59 |         __COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_macro.h:210:9: note: in expansion of macro 'Z_COND_CODE_1'
  210 |         Z_COND_CODE_1(_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~~~
/home/user/zephyr/drivers/flash/flash_priv.h:21:9: note: in expansion of macro 'COND_CODE_1'
   21 |         COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, soc_nv_flash), (node_id), ())
      |         ^~~~~~~~~~~
/tmp/build-lpem-base/zephyr/include/generated/zephyr/devicetree_generated.h:7705:78: note: in expansion of macro 'SOC_NV_FLASH_COMPAT_NODE'
 7705 | #define DT_N_S_soc_S_flash_controller_40021000_FOREACH_CHILD_STATUS_OKAY(fn) fn(DT_N_S_soc_S_flash_controller_40021000_S_flash_0) fn(DT_N_S_soc_S_flash_controller_40021000_S_flash_4e020000)
      |                                                                              ^~
/home/user/zephyr/include/zephyr/devicetree.h:6280:24: note: in expansion of macro 'DT_CAT'
 6280 | #define DT_CAT(a1, a2) a1 ## a2
      |                        ^~
/home/user/zephyr/include/zephyr/devicetree.h:3801:9: note: in expansion of macro 'DT_CAT'
 3801 |         DT_CAT(node_id, _FOREACH_CHILD_STATUS_OKAY)(fn)
      |         ^~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:4832:9: note: in expansion of macro 'DT_FOREACH_CHILD_STATUS_OKAY'
 4832 |         DT_FOREACH_CHILD_STATUS_OKAY(DT_DRV_INST(inst), fn)
      |         ^~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:146:36: note: in expansion of macro 'DT_N_INST_0_ti_cc23x0_flash_controller'
  146 | #define UTIL_PRIMITIVE_CAT(a, ...) a##__VA_ARGS__
      |                                    ^
/home/user/zephyr/include/zephyr/sys/util_internal.h:145:26: note: in expansion of macro 'UTIL_CAT'
  145 | #define UTIL_CAT(a, ...) UTIL_PRIMITIVE_CAT(a, __VA_ARGS__)
      |                          ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:401:31: note: in expansion of macro 'DT_INST'
  401 | #define DT_INST(inst, compat) UTIL_CAT(DT_N_INST, DT_DASH(inst, compat))
      |                               ^~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:4630:27: note: in expansion of macro 'DT_DRV_INST'
 4630 | #define DT_DRV_INST(inst) DT_INST(inst, DT_DRV_COMPAT)
      |                           ^~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:4832:38: note: in expansion of macro 'DT_FOREACH_CHILD_STATUS_OKAY'
 4832 |         DT_FOREACH_CHILD_STATUS_OKAY(DT_DRV_INST(inst), fn)
      |                                      ^~~~~~~~~~~
/home/user/zephyr/drivers/flash/flash_priv.h:29:9: note: in expansion of macro 'DT_INST_FOREACH_CHILD_STATUS_OKAY'
   29 |         DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, SOC_NV_FLASH_COMPAT_NODE)
      |         ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/drivers/flash/soc_flash_cc23x0.c:20:27: note: in expansion of macro 'SOC_NV_FLASH_CHILD_NODE'
   20 | #define SOC_NV_FLASH_NODE SOC_NV_FLASH_CHILD_NODE(0)
      |                           ^~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/drivers/flash/soc_flash_cc23x0.c:25:34: note: in expansion of macro 'SOC_NV_FLASH_NODE'
   25 | #define FLASH_WRITE_SIZE DT_PROP(SOC_NV_FLASH_NODE, write_block_size)
      |                                  ^~~~~~~~~~~~~~~~~
/home/user/zephyr/drivers/flash/soc_flash_cc23x0.c:32:29: note: in expansion of macro 'FLASH_WRITE_SIZE'
   32 |         .write_block_size = FLASH_WRITE_SIZE,
      |                             ^~~~~~~~~~~~~~~~
In file included from /home/user/zephyr/include/zephyr/devicetree.h:20:
/tmp/build-lpem-base/zephyr/include/generated/zephyr/devicetree_generated.h:8135:84: error: expected '}' before numeric constant
 8135 | #define DT_N_S_soc_S_flash_controller_40021000_S_flash_4e020000_P_write_block_size 16
      |                                                                                    ^~
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-lpem-base
```

dtdoctor diagnosis:

```
+-----------------------------------------------------------------------------------------------------------+
| DT Doctor                                                                                                 |
+===========================================================================================================+
| 'flash0: /soc/flash-controller@40021000/flash@0' is enabled but no driver appears to be available for it. |
+-----------------------------------------------------------------------------------------------------------+
```

Same "enabled but no driver" family as `kit_t2g_b_h_evk`, in the same flash-child-node
shape (`soc_nv_flash` compat lookup over `flash-controller` children).

### heltec_wifi_lora32_v2

- **Board:** `heltec_wifi_lora32_v2/esp32/appcpu`
- **Test:** `tests/drivers/build_all/led` (`drivers.led.build`)

Baseline error:

```
In file included from /home/user/zephyr/include/zephyr/device.h:12,
                 from /home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c:7:
/home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c: In function 'board_heltec_wifi_lora32_v2_init':
/home/user/zephyr/include/zephyr/devicetree.h:197:36: error: 'DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin' undeclared (first use in this function)
  197 | #define DT_NODELABEL(label) DT_CAT(DT_N_NODELABEL_, label)
      |                                    ^~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:6291:9: note: in definition of macro 'DT_CAT7'
 6291 |         a1 ## a2 ## a3 ## a4 ## a5 ## a6 ## a7
      |         ^~
/home/user/zephyr/include/zephyr/devicetree/gpio.h:114:9: note: in expansion of macro 'DT_PHA_BY_IDX'
  114 |         DT_PHA_BY_IDX(node_id, gpio_pha, idx, pin)
      |         ^~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree/gpio.h:125:9: note: in expansion of macro 'DT_GPIO_PIN_BY_IDX'
  125 |         DT_GPIO_PIN_BY_IDX(node_id, gpio_pha, 0)
      |         ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c:11:19: note: in expansion of macro 'DT_GPIO_PIN'
   11 | #define VEXT_PIN  DT_GPIO_PIN(DT_NODELABEL(vext), gpios)
      |                   ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:197:29: note: in expansion of macro 'DT_CAT'
  197 | #define DT_NODELABEL(label) DT_CAT(DT_N_NODELABEL_, label)
      |                             ^~~~~~
/home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c:11:31: note: in expansion of macro 'DT_NODELABEL'
   11 | #define VEXT_PIN  DT_GPIO_PIN(DT_NODELABEL(vext), gpios)
      |                               ^~~~~~~~~~~~
/home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c:24:34: note: in expansion of macro 'VEXT_PIN'
   24 |         gpio_pin_configure(gpio, VEXT_PIN, GPIO_OUTPUT);
      |                                  ^~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:197:36: note: each undeclared identifier is reported only once for each function it appears in
[... repeats similarly for OLED_RST / DT_N_NODELABEL_oledrst_P_gpios_IDX_0_VAL_pin ...]
/home/user/zephyr/include/zephyr/devicetree.h:197:36: error: 'DT_N_NODELABEL_oledrst_P_gpios_IDX_0_VAL_pin' undeclared (first use in this function)
  197 | #define DT_NODELABEL(label) DT_CAT(DT_N_NODELABEL_, label)
...
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-heltec_wifi_lora32_v2-base
```

dtdoctor diagnosis:

```
/home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c: In function 'board_heltec_wifi_lora32_v2_init':
/home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c:24:34: error: 'DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin' undeclared (first use in this function)
   24 |         gpio_pin_configure(gpio, VEXT_PIN, GPIO_OUTPUT);
      |                                  ^~~~~~~~
/home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c:24:34: note: each undeclared identifier is reported only once for each function it appears in
/home/user/zephyr/boards/heltec/heltec_wifi_lora32_v2/board_init.c:28:34: error: 'DT_N_NODELABEL_oledrst_P_gpios_IDX_0_VAL_pin' undeclared (first use in this function)
   28 |         gpio_pin_configure(gpio, OLED_RST, GPIO_OUTPUT);
      |                                  ^~~~~~~~
+-----------------------------------------------------------------------------------+
| DT Doctor                                                                         |
+===================================================================================+
| No node label 'oledrst' exists in this build's devicetree.                        |
|                                                                                   |
| Node labels are the 'name:' part written in front of a node in a DTS file, e.g.   |
| 'my_serial: uart@40002000'. In C they are lowercased, so DT_NODELABEL(my_serial). |
|                                                                                   |
| Node labels can be added to an existing node from a devicetree overlay:           |
|                                                                                   |
|     my_serial: &uart0 {};                                                         |
|                                                                                   |
| See <build>/zephyr/zephyr.dts for the devicetree this build actually used.        |
+-----------------------------------------------------------------------------------+
+-----------------------------------------------------------------------------------+
| DT Doctor                                                                         |
+===================================================================================+
| No node label 'vext' exists in this build's devicetree.                           |
|                                                                                   |
| Node labels are the 'name:' part written in front of a node in a DTS file, e.g.   |
| 'my_serial: uart@40002000'. In C they are lowercased, so DT_NODELABEL(my_serial). |
|                                                                                   |
| Node labels can be added to an existing node from a devicetree overlay:           |
|                                                                                   |
|     my_serial: &uart0 {};                                                         |
|                                                                                   |
| See <build>/zephyr/zephyr.dts for the devicetree this build actually used.        |
+-----------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-heltec_wifi_lora32_v2-dtd
```

dtdoctor correctly diagnosed both missing node labels (`vext` and `oledrst`)
for the `appcpu` qualifier build, emitting one "node-label-does-not-exist" box
per leaked macro reference.

### esp32p4x_lp_uart

- **Board:** `esp32p4x_function_ev_board/esp32p4/hpcore`
- **Test:** `tests/boards/espressif/lp_uart` (`boards.espressif.lp_uart.loopback`)

Baseline error:

```
In file included from /home/user/zephyr/include/zephyr/toolchain/gcc.h:98,
                 from /home/user/zephyr/include/zephyr/toolchain.h:66,
                 from /home/user/zephyr/include/zephyr/kernel_includes.h:23,
                 from /home/user/zephyr/include/zephyr/kernel.h:17,
                 from /home/user/zephyr/tests/boards/espressif/lp_uart/src/main.c:7:
/home/user/zephyr/include/zephyr/device.h:96:41: error: '__device_dts_ord_75' undeclared here (not in a function); did you mean '__device_dts_ord_15'?
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                         ^~~~~~~~~
/home/user/zephyr/include/zephyr/toolchain/common.h:188:26: note: in definition of macro '_DO_CONCAT'
  188 | #define _DO_CONCAT(x, y) x ## y
      |                          ^
/home/user/zephyr/include/zephyr/device.h:96:33: note: in expansion of macro '_CONCAT'
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                 ^~~~~~~
/home/user/zephyr/include/zephyr/device.h:300:37: note: in expansion of macro 'DEVICE_DT_NAME_GET'
  300 | #define DEVICE_DT_NAME_GET(node_id) DEVICE_NAME_GET(Z_DEVICE_DT_DEV_ID(node_id))
      |                                     ^~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/device.h:317:34: note: in expansion of macro 'DEVICE_DT_GET'
  317 | #define DEVICE_DT_GET(node_id) (&DEVICE_DT_NAME_GET(node_id))
      |                                  ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/tests/boards/espressif/lp_uart/src/main.c:18:45: note: in expansion of macro 'DEVICE_DT_GET'
   18 | static const struct device *const lp_uart = DEVICE_DT_GET(LP_UART_NODE);
      |                                             ^~~~~~~~~~~~~
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-esp32p4x_lp_uart-base
```

dtdoctor diagnosis:

```
/home/user/zephyr/tests/boards/espressif/lp_uart/src/main.c:18:45: error: '__device_dts_ord_75' undeclared here (not in a function); did you mean '__device_dts_ord_15'?
   18 | static const struct device *const lp_uart = DEVICE_DT_GET(LP_UART_NODE);
      |                                             ^~~~~~~~~~~~~
      |                                             __device_dts_ord_15
+--------------------------------------------------------------------------------------------------------------------+
| DT Doctor                                                                                                          |
+====================================================================================================================+
| 'lp_uart: /soc/uart@50121000' is disabled in /home/user/zephyr/dts/riscv/espressif/esp32p4/esp32p4_common.dtsi:622 |
|                                                                                                                    |
| Try enabling the node by setting its 'status' property to 'okay'.                                                  |
+--------------------------------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-esp32p4x_lp_uart-dtd
```

Another "disabled node" diagnosis, this time with no dependent nodes listed
(the referenced node is disabled and has no children pulling it in transitively),
pointing straight at the `.dtsi` line where `status` is not `okay`.

### esp32p4x_spi

- **Board:** `esp32p4x_function_ev_board/esp32p4/hpcore`
- **Test:** `tests/drivers/spi/spi_controller_peripheral` (`drivers.spi.spi_esp32`, `-DCONFIG_TESTED_SPI_MODE=0`)

Baseline error:

```
In file included from /home/user/zephyr/include/zephyr/toolchain/gcc.h:98,
                 from /home/user/zephyr/include/zephyr/toolchain.h:66,
                 from /home/user/zephyr/include/zephyr/kernel_includes.h:23,
                 from /home/user/zephyr/include/zephyr/kernel.h:17,
                 from /home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:9:
/home/user/zephyr/include/zephyr/device.h:96:41: error: '__device_dts_ord_DT_N_NODELABEL_dut_spi_dt_BUS_ORD' undeclared here (not in a function)
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                         ^~~~~~~~~
/home/user/zephyr/include/zephyr/toolchain/common.h:188:26: note: in definition of macro '_DO_CONCAT'
  188 | #define _DO_CONCAT(x, y) x ## y
      |                          ^
/home/user/zephyr/include/zephyr/device.h:96:33: note: in expansion of macro '_CONCAT'
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                 ^~~~~~~
/home/user/zephyr/include/zephyr/device.h:300:37: note: in expansion of macro 'DEVICE_DT_NAME_GET'
  300 | #define DEVICE_DT_NAME_GET(node_id) DEVICE_NAME_GET(Z_DEVICE_DT_DEV_ID(node_id))
      |                                     ^~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/device.h:317:34: note: in expansion of macro 'DEVICE_DT_NAME_GET'
  317 | #define DEVICE_DT_GET(node_id) (&DEVICE_DT_NAME_GET(node_id))
      |                                  ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/drivers/spi.h:563:24: note: in expansion of macro 'DEVICE_DT_GET'
  563 |                 .bus = DEVICE_DT_GET(DT_BUS(node_id)),                          \
      |                        ^~~~~~~~~~~~~
/home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:33:34: note: in expansion of macro 'SPI_DT_SPEC_GET'
   33 | static struct spi_dt_spec spim = SPI_DT_SPEC_GET(DT_NODELABEL(dut_spi_dt), SPIM_OP);
      |                                  ^~~~~~~~~~~~~~~
In file included from /home/user/zephyr/include/zephyr/device.h:12,
                 from /home/user/zephyr/include/zephyr/sw_isr_table.h:18,
                 from /home/user/zephyr/include/zephyr/arch/riscv/irq.h:25,
                 from /home/user/zephyr/include/zephyr/arch/riscv/arch.h:23,
                 from /home/user/zephyr/include/zephyr/arch/cpu.h:23,
                 from /home/user/zephyr/include/zephyr/kernel_includes.h:36:
/home/user/zephyr/include/zephyr/devicetree.h:197:36: error: 'DT_N_NODELABEL_dut_spi_dt_P_spi_max_frequency' undeclared here (not in a function)
[... additional cascading DT_N_NODELABEL_dut_spi_dt_* undeclared errors follow for P_duplex, P_frame_format, REG_IDX_0_VAL_ADDRESSU, P_spi_interframe_delay_ns, and __device_dts_ord_DT_N_NODELABEL_dut_spis_ORD for the DEVICE_DT_GET(DT_NODELABEL(dut_spis)) reference at main.c:34]
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-esp32p4x_spi-base
```

dtdoctor diagnosis:

```
/home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:33:34: error: '__device_dts_ord_DT_N_NODELABEL_dut_spi_dt_BUS_ORD' undeclared here (not in a function)
   33 | static struct spi_dt_spec spim = SPI_DT_SPEC_GET(DT_NODELABEL(dut_spi_dt), SPIM_OP);
      |                                  ^~~~~~~~~~~~~~~
/home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:33:34: error: 'DT_N_NODELABEL_dut_spi_dt_P_spi_max_frequency' undeclared here (not in a function)
/home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:33:34: error: 'DT_N_NODELABEL_dut_spi_dt_P_duplex' undeclared here (not in a function)
/home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:33:34: error: 'DT_N_NODELABEL_dut_spi_dt_P_frame_format' undeclared here (not in a function)
/home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:33:34: error: 'DT_N_NODELABEL_dut_spi_dt_REG_IDX_0_VAL_ADDRESSU' undeclared here (not in a function)
/home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:33:34: error: 'DT_N_NODELABEL_dut_spi_dt_P_spi_interframe_delay_ns' undeclared here (not in a function)
/home/user/zephyr/tests/drivers/spi/spi_controller_peripheral/src/main.c:34:40: error: '__device_dts_ord_DT_N_NODELABEL_dut_spis_ORD' undeclared here (not in a function)
   34 | static const struct device *spis_dev = DEVICE_DT_GET(DT_NODELABEL(dut_spis));
      |                                        ^~~~~~~~~~~~~
+-----------------------------------------------------------------------------------+
| DT Doctor                                                                         |
+===================================================================================+
| No node label 'dut_spi_dt' exists in this build's devicetree.                     |
|                                                                                   |
| Node labels are the 'name:' part written in front of a node in a DTS file, e.g.   |
| 'my_serial: uart@40002000'. In C they are lowercased, so DT_NODELABEL(my_serial). |
|                                                                                   |
| Node labels can be added to an existing node from a devicetree overlay:           |
|                                                                                   |
|     my_serial: &uart0 {};                                                         |
|                                                                                   |
| See <build>/zephyr/zephyr.dts for the devicetree this build actually used.        |
+-----------------------------------------------------------------------------------+
(this "DT Doctor" box for 'dut_spi_dt' repeats 6 times, once per leaked macro reference above)
+-----------------------------------------------------------------------------------+
| DT Doctor                                                                         |
+===================================================================================+
| No node label 'dut_spis' exists in this build's devicetree.                       |
|                                                                                   |
| Node labels are the 'name:' part written in front of a node in a DTS file, e.g.   |
| 'my_serial: uart@40002000'. In C they are lowercased, so DT_NODELABEL(my_serial). |
|                                                                                   |
| Node labels can be added to an existing node from a devicetree overlay:           |
|                                                                                   |
|     my_serial: &uart0 {};                                                         |
|                                                                                   |
| See <build>/zephyr/zephyr.dts for the devicetree this build actually used.        |
+-----------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-esp32p4x_spi-dtd
```

Two node labels, `dut_spi_dt` and `dut_spis`, are both absent from this
board/qualifier's devicetree; dtdoctor's "node-label-does-not-exist" diagnosis
fired once per leaked macro (7 total boxes), correctly attributing all of them
to just two missing labels.

### 96b_wistrio

- **Board:** `96b_wistrio/stm32l151xba`
- **Test:** `samples/subsys/lorawan/class_a` (`CONFIG_LORA_MODULE_BACKEND_NATIVE=y CONFIG_LORAWAN_REGION_EU868=y`)

Baseline error:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: app/libapp.a(main.c.obj): in function `z_log_msg_simple_create_0':
/tmp/build-96b_wistrio-base/zephyr/include/generated/zephyr/syscalls/log_msg.h:37:(.text.main+0x164): undefined reference to `__device_dts_ord_80'
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: zephyr/subsys/lorawan/native/libsubsys__lorawan__native.a(lorawan.c.obj): in function `lorawan_start':
/home/user/zephyr/subsys/lorawan/native/lorawan.c:78:(.text.lorawan_start+0xc4): undefined reference to `__device_dts_ord_80'
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-96b_wistrio-base
```

dtdoctor diagnosis:

```
+-------------------------------------------------------------------------------------------+
| DT Doctor                                                                                 |
+===========================================================================================+
| 'lora: /soc/spi@40013000/lora@0' is enabled but no driver appears to be available for it. |
|                                                                                           |
| Try enabling these Kconfig options:                                                       |
|                                                                                           |
|  - CONFIG_DT_HAS_SEMTECH_LLCC68_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1261_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1262_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1268_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1272_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1278_ENABLED=y                                                 |
|  - CONFIG_LORA=y                                                                          |
|  - CONFIG_LORA_SX127X=y                                                                   |
|  - CONFIG_ZEPHYR_LORA_BASICS_MODEM_MODULE=y                                               |
+-------------------------------------------------------------------------------------------+
```

"Enabled but no driver" family: `CONFIG_LORA_MODULE_BACKEND_NATIVE=y` does not
by itself select `CONFIG_LORA`/`CONFIG_LORA_SX127X`, so the enabled `lora@0`
node has nothing bound to it and dtdoctor lists all the candidate transceiver
Kconfig symbols.

### adafruit_feather_rfm95_rp2040

- **Board:** `adafruit_feather_rfm95_rp2040/rp2040`
- **Test:** `samples/subsys/lorawan/class_a` (`-DCONFIG_LORA_MODULE_BACKEND_NATIVE=y -DCONFIG_LORAWAN_REGION_EU868=y`)

Baseline error:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: app/libapp.a(main.c.obj): in function `z_log_msg_simple_create_0':
/tmp/build-adafruit_feather_rfm95_rp2040-base/zephyr/include/generated/zephyr/syscalls/log_msg.h:37:(.text.main+0x160): undefined reference to `__device_dts_ord_86'
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: zephyr/subsys/lorawan/native/libsubsys__lorawan__native.a(lorawan.c.obj): in function `lorawan_start':
/home/user/zephyr/subsys/lorawan/native/lorawan.c:78:(.text.lorawan_start+0xa4): undefined reference to `__device_dts_ord_86'
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-adafruit_feather_rfm95_rp2040-base
```

dtdoctor diagnosis:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: app/libapp.a(main.c.obj): in function `z_log_msg_simple_create_0':
/tmp/build-adafruit_feather_rfm95_rp2040-dtd/zephyr/include/generated/zephyr/syscalls/log_msg.h:37:(.text.main+0x160): undefined reference to `__device_dts_ord_86'
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: zephyr/subsys/lorawan/native/libsubsys__lorawan__native.a(lorawan.c.obj): in function `lorawan_start':
/home/user/zephyr/subsys/lorawan/native/lorawan.c:78:(.text.lorawan_start+0xa4): undefined reference to `__device_dts_ord_86'
collect2: error: ld returned 1 exit status
+--------------------------------------------------------------------------------------------+
| DT Doctor                                                                                  |
+============================================================================================+
| 'lora: /soc/spi@40040000/radio@0' is enabled but no driver appears to be available for it. |
|                                                                                            |
| Try enabling these Kconfig options:                                                        |
|                                                                                            |
|  - CONFIG_DT_HAS_SEMTECH_LLCC68_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1261_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1262_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1268_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1272_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1278_ENABLED=y                                                  |
|  - CONFIG_LORA=y                                                                           |
|  - CONFIG_LORA_SX127X=y                                                                    |
|  - CONFIG_ZEPHYR_LORA_BASICS_MODEM_MODULE=y                                                |
+--------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-adafruit_feather_rfm95_rp2040-dtd
```

Same "enabled but no driver" family and node shape as `96b_wistrio`, on a
different board/SoC (`radio@0` under `spi@40040000` on rp2040).

### bytesensi_l

- **Board:** `bytesensi_l/nrf52832`
- **Test:** `samples/subsys/lorawan/class_a` (`CONFIG_LORA_MODULE_BACKEND_NATIVE=y CONFIG_LORAWAN_REGION_EU868=y`)

Baseline error:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: app/libapp.a(main.c.obj): in function `z_log_msg_simple_create_0':
/tmp/build-bytesensi_l-base/zephyr/include/generated/zephyr/syscalls/log_msg.h:37:(.text.main+0x168): undefined reference to `__device_dts_ord_90'
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: zephyr/subsys/lorawan/native/libsubsys__lorawan__native.a(lorawan.c.obj): in function `lorawan_start':
/home/user/zephyr/subsys/lorawan/native/lorawan.c:78:(.text.lorawan_start+0xc4): undefined reference to `__device_dts_ord_90'
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-bytesensi_l-base
```

dtdoctor diagnosis:

```
+-------------------------------------------------------------------------------------------+
| DT Doctor                                                                                 |
+===========================================================================================+
| 'lora: /soc/spi@40004000/lora@0' is enabled but no driver appears to be available for it. |
|                                                                                           |
| Try enabling these Kconfig options:                                                       |
|                                                                                           |
|  - CONFIG_DT_HAS_SEMTECH_LLCC68_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1261_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1262_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1268_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1272_ENABLED=y                                                 |
|  - CONFIG_DT_HAS_SEMTECH_SX1278_ENABLED=y                                                 |
|  - CONFIG_LORA=y                                                                          |
|  - CONFIG_LORA_SX127X=y                                                                   |
|  - CONFIG_ZEPHYR_LORA_BASICS_MODEM_MODULE=y                                               |
+-------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-bytesensi_l-dtd
```

Same "enabled but no driver" family, same recurring `CONFIG_LORA_MODULE_BACKEND_NATIVE`
root cause, now on nrf52832.

### rm1xx_dvk

- **Board:** `rm1xx_dvk/nrf51822`
- **Test:** `samples/subsys/lorawan/class_a` (`-DCONFIG_LORA_MODULE_BACKEND_NATIVE=y -DCONFIG_LORAWAN_REGION_EU868=y`)

Baseline error:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: app/libapp.a(main.c.obj): in function `z_log_msg_simple_create_0':
/tmp/build-rm1xx_dvk-base/zephyr/include/generated/zephyr/syscalls/log_msg.h:37:(.text.main+0x164): undefined reference to `__device_dts_ord_74'
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: zephyr/subsys/lorawan/native/libsubsys__lorawan__native.a(lorawan.c.obj): in function `lorawan_start':
/home/user/zephyr/subsys/lorawan/native/lorawan.c:78:(.text.lorawan_start+0xa4): undefined reference to `__device_dts_ord_74'
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-rm1xx_dvk-base
```

dtdoctor diagnosis:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: app/libapp.a(main.c.obj): in function `z_log_msg_simple_create_0':
/tmp/build-rm1xx_dvk-dtd/zephyr/include/generated/zephyr/syscalls/log_msg.h:37:(.text.main+0x164): undefined reference to `__device_dts_ord_74'
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/14.3.0/../../../../arm-zephyr-eabi/bin/ld.bfd: zephyr/subsys/lorawan/native/libsubsys__lorawan__native.a(lorawan.c.obj): in function `lorawan_start':
/home/user/zephyr/subsys/lorawan/native/lorawan.c:78:(.text.lorawan_start+0xa4): undefined reference to `__device_dts_ord_74'
collect2: error: ld returned 1 exit status
+--------------------------------------------------------------------------------------------+
| DT Doctor                                                                                  |
+============================================================================================+
| 'lora0: /soc/spi@40004000/lora@1' is enabled but no driver appears to be available for it. |
|                                                                                            |
| Try enabling these Kconfig options:                                                        |
|                                                                                            |
|  - CONFIG_DT_HAS_SEMTECH_LLCC68_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1261_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1262_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1268_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1276_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1278_ENABLED=y                                                  |
|  - CONFIG_LORA=y                                                                           |
|  - CONFIG_LORA_SX127X=y                                                                    |
|  - CONFIG_ZEPHYR_LORA_BASICS_MODEM_MODULE=y                                                |
+--------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-rm1xx_dvk-dtd
```

Same family, `lora@1` this time (a different node address than the other LoRa
boards, sharing the same missing `CONFIG_LORA`/`CONFIG_LORA_SX127X` root cause).

### ttgo_lora32

- **Board:** `ttgo_lora32/esp32/procpu`
- **Test:** `samples/subsys/lorawan/class_a` (`-DCONFIG_LORA_MODULE_BACKEND_NATIVE=y -DCONFIG_LORAWAN_REGION_EU868=y`)

Baseline error:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/xtensa-espressif_esp32_zephyr-elf/bin/../lib/gcc/xtensa-espressif_esp32_zephyr-elf/14.3.0/../../../../xtensa-espressif_esp32_zephyr-elf/bin/ld.bfd: app/libapp.a(main.c.obj):(.literal.main+0xc): undefined reference to `__device_dts_ord_100'
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-ttgo_lora32-base
```

dtdoctor diagnosis:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/xtensa-espressif_esp32_zephyr-elf/bin/../lib/gcc/xtensa-espressif_esp32_zephyr-elf/14.3.0/../../../../xtensa-espressif_esp32_zephyr-elf/bin/ld.bfd: app/libapp.a(main.c.obj):(.literal.main+0xc): undefined reference to `__device_dts_ord_100'
collect2: error: ld returned 1 exit status
+--------------------------------------------------------------------------------------------+
| DT Doctor                                                                                  |
+============================================================================================+
| 'lora0: /soc/spi@3ff65000/lora@0' is enabled but no driver appears to be available for it. |
|                                                                                            |
| Try enabling these Kconfig options:                                                        |
|                                                                                            |
|  - CONFIG_DT_HAS_SEMTECH_LLCC68_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1261_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1262_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1268_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1272_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1278_ENABLED=y                                                  |
|  - CONFIG_LORA=y                                                                           |
|  - CONFIG_LORA_SX127X=y                                                                    |
|  - CONFIG_ZEPHYR_LORA_BASICS_MODEM_MODULE=y                                                |
+--------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-ttgo_lora32-dtd
```

Same "enabled but no driver" family, on esp32/procpu.

### ttgo_tbeam

- **Board:** `ttgo_tbeam/esp32/procpu`
- **Test:** `samples/subsys/lorawan/class_a` (`-DCONFIG_LORA_MODULE_BACKEND_NATIVE=y -DCONFIG_LORAWAN_REGION_EU868=y`)

Baseline error:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/xtensa-espressif_esp32_zephyr-elf/bin/../lib/gcc/xtensa-espressif_esp32_zephyr-elf/14.3.0/../../../../xtensa-espressif_esp32_zephyr-elf/bin/ld.bfd: app/libapp.a(main.c.obj):(.literal.main+0xc): undefined reference to `__device_dts_ord_106'
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-ttgo_tbeam-base
```

dtdoctor diagnosis:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/xtensa-espressif_esp32_zephyr-elf/bin/../lib/gcc/xtensa-espressif_esp32_zephyr-elf/14.3.0/../../../../xtensa-espressif_esp32_zephyr-elf/bin/ld.bfd: app/libapp.a(main.c.obj):(.literal.main+0xc): undefined reference to `__device_dts_ord_106'
collect2: error: ld returned 1 exit status
+--------------------------------------------------------------------------------------------+
| DT Doctor                                                                                  |
+============================================================================================+
| 'lora0: /soc/spi@3ff65000/lora@0' is enabled but no driver appears to be available for it. |
|                                                                                            |
| Try enabling these Kconfig options:                                                        |
|                                                                                            |
|  - CONFIG_DT_HAS_SEMTECH_LLCC68_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1261_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1262_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1268_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1272_ENABLED=y                                                  |
|  - CONFIG_DT_HAS_SEMTECH_SX1278_ENABLED=y                                                  |
|  - CONFIG_LORA=y                                                                           |
|  - CONFIG_LORA_SX127X=y                                                                    |
|  - CONFIG_ZEPHYR_LORA_BASICS_MODEM_MODULE=y                                                |
+--------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-ttgo_tbeam-dtd
```

Same "enabled but no driver" family and node shape as `ttgo_lora32`, different
device ordinal (`__device_dts_ord_106` vs `_100`).

### serpente

- **Board:** `serpente/samd21e18a`
- **Test:** `tests/drivers/adc/adc_api` (`drivers.adc`)

Baseline error:

```
In file included from /home/user/zephyr/include/zephyr/toolchain/gcc.h:98,
                 from /home/user/zephyr/include/zephyr/toolchain.h:66,
                 from /home/user/zephyr/include/zephyr/sys/__assert.h:11,
                 from /home/user/zephyr/include/zephyr/irq_multilevel.h:15,
                 from /home/user/zephyr/include/zephyr/devicetree.h:21,
                 from /home/user/zephyr/include/zephyr/device.h:12,
                 from /home/user/zephyr/include/zephyr/drivers/adc.h:17,
                 from /home/user/zephyr/tests/drivers/adc/adc_api/src/test_adc.c:9:
/home/user/zephyr/include/zephyr/device.h:96:41: error: '__device_dts_ord_9' undeclared here (not in a function); did you mean '__device_dts_ord_3'?
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                         ^~~~~~~~~
/home/user/zephyr/include/zephyr/toolchain/common.h:188:26: note: in definition of macro '_DO_CONCAT'
  188 | #define _DO_CONCAT(x, y) x ## y
      |                          ^
/home/user/zephyr/include/zephyr/device.h:96:33: note: in expansion of macro '_CONCAT'
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                 ^~~~~~~
/home/user/zephyr/include/zephyr/device.h:300:37: note: in expansion of macro 'DEVICE_NAME_GET'
  300 | #define DEVICE_DT_NAME_GET(node_id) DEVICE_NAME_GET(Z_DEVICE_DT_DEV_ID(node_id))
      |                                     ^~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/device.h:317:34: note: in expansion of macro 'DEVICE_DT_NAME_GET'
  317 | #define DEVICE_DT_GET(node_id) (&DEVICE_DT_NAME_GET(node_id))
      |                                  ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/drivers/adc.h:345:24: note: in expansion of macro 'DEVICE_DT_GET'
  345 |                 .dev = DEVICE_DT_GET(ctlr), \
      |                        ^~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/drivers/adc.h:550:9: note: in expansion of macro 'ADC_DT_SPEC_STRUCT'
  550 |         ADC_DT_SPEC_STRUCT(DT_IO_CHANNELS_CTLR_BY_IDX(node_id, idx), \
      |         ^~~~~~~~~~~~~~~~~~
/home/user/zephyr/tests/drivers/adc/adc_api/src/test_adc.c:43:49: note: in expansion of macro 'ADC_DT_SPEC_GET_BY_IDX'
   43 | #define DT_SPEC_AND_COMMA(node_id, prop, idx)   ADC_DT_SPEC_GET_BY_IDX(node_id, idx),
      |                                                 ^~~~~~~~~~~~~~~~~~~~~~
/tmp/build-serpente-base/zephyr/include/generated/zephyr/devicetree_generated.h:1497:64: note: in expansion of macro 'DT_SPEC_AND_COMMA'
 1497 | #define DT_N_S_zephyr_user_P_io_channels_FOREACH_PROP_ELEM(fn) fn(DT_N_S_zephyr_user, io_channels, 0)
      |                                                                ^~
/home/user/zephyr/include/zephyr/devicetree.h:6284:33: note: in expansion of macro 'DT_CAT4'
 6284 | #define DT_CAT4(a1, a2, a3, a4) a1 ## a2 ## a3 ## a4
      |                                 ^~
/home/user/zephyr/tests/drivers/adc/adc_api/src/test_adc.c:48:9: note: in expansion of macro 'DT_FOREACH_PROP_ELEM'
   48 |         DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)
      |         ^~~~~~~~~~~~~~~~~~~~
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-serpente-base
```

dtdoctor diagnosis:

```
/home/user/zephyr/tests/drivers/adc/adc_api/src/test_adc.c:48:9: error: '__device_dts_ord_9' undeclared here (not in a function); did you mean '__device_dts_ord_3'?
   48 |         DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)
      |         ^~~~~~~~~~~~~~~~~~~~
      |         __device_dts_ord_3
+-----------------------------------------------------------------------------------------+
| DT Doctor                                                                               |
+=========================================================================================+
| 'adc: /soc/adc@42004000' is disabled in /home/user/zephyr/dts/arm/atmel/samd2x.dtsi:210 |
| The following nodes depend on it:                                                       |
|  - /zephyr,user                                                                         |
|  - /soc/adc@42004000/channel@0                                                          |
|                                                                                         |
| It is referenced by the following aliases: 'adc-0'                                      |
|                                                                                         |
| Try enabling the node by setting its 'status' property to 'okay'.                       |
+-----------------------------------------------------------------------------------------+
```

Another "disabled ancestor" diagnosis, notable for listing two dependent
nodes (`/zephyr,user` and the `channel@0` subnode) plus the `adc-0` alias that
also points at the disabled node — the fullest walk of the dependency graph
seen in this sweep.

### pt2_sifli

- **Board:** `pt2/sf32lb52jud6`
- **Test:** `tests/drivers/uart/uart_async_api` (`drivers.uart.async_api`)

Baseline error:

```
/home/user/zephyr/drivers/serial/uart_sf32lb.c:955:16: error: implicit declaration of function... (build continues)
...
[155/182] Building C object zephyr/drivers/serial/CMakeFiles/drivers__serial.dir/uart_sf32lb.c.obj
FAILED: zephyr/drivers/serial/CMakeFiles/drivers__serial.dir/uart_sf32lb.c.obj
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc -DK_HEAP_MEM_POOL_SIZE=0 -DPICOLIBC_LONG_LONG_PRINTF_SCANF -DSF32LB52X -DSOC_BF0_HCPU ... -c /home/user/zephyr/drivers/serial/uart_sf32lb.c
In file included from /home/user/zephyr/include/zephyr/sys/util_macro.h:34,
                 from /home/user/zephyr/include/zephyr/arch/arm/syscall.h:31,
                 from /home/user/zephyr/include/zephyr/arch/syscall.h:19,
                 from /home/user/zephyr/include/zephyr/arch/arch_interface.h:760,
                 from /home/user/zephyr/include/zephyr/arch/cpu.h:12,
                 from /home/user/zephyr/drivers/serial/uart_sf32lb.c:9:
/home/user/zephyr/include/zephyr/device.h:96:41: error: '__device_dts_ord_DT_N_S_soc_S_serial_50084000_P_dmas_NAME_tx_PH_ORD' undeclared here (not in a function)
   96 | #define DEVICE_NAME_GET(dev_id) _CONCAT(__device_, dev_id)
      |                                         ^~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:72:26: note: in definition of macro '__DEBRACKET'
   72 | #define __DEBRACKET(...) __VA_ARGS__
      |                          ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:64:9: note: in expansion of macro '__GET_ARG2_DEBRACKET'
   64 |         __GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)
      |         ^~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:59:9: note: in expansion of macro '__COND_CODE'
   59 |         __COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_macro.h:210:9: note: in expansion of macro 'Z_COND_CODE_1'
  210 |         Z_COND_CODE_1(_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:5966:9: note: in expansion of macro 'COND_CODE_1'
 5966 |         COND_CODE_1(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT),   \
      |         ^~~~~~~~~~~
... (macro expansion chain continues through DT_INST_DEV_ID / SF32LB_DMA_DT_INST_SPEC_GET_BY_NAME / uart_sf32lb.c:950 SF32LB_UART_DEFINE / uart_sf32lb.c:959 DT_INST_FOREACH_STATUS_OKAY(SF32LB_UART_DEFINE)) ...
/tmp/build-pt2sifli-base/zephyr/include/generated/zephyr/devicetree_generated.h:5942:40: error: 'DT_N_S_soc_S_serial_50084000_P_dmas_NAME_rx_VAL_config' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_serial_50084000_P_dma_coherent_EXISTS'?
 5942 | #define DT_N_INST_0_sifli_sf32lb_usart DT_N_S_soc_S_serial_50084000
      |                                        ^~~~~~~~~~~~~~~~~~~~~~~~~~~~
... (further COND_CODE_1/DT_PHA_BY_NAME/DT_DMAS_CELL_BY_NAME/SF32LB_DMA_DT_SPEC_GET_BY_NAME expansion notes for the rx dmas cell) ...
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-pt2sifli-base
```

dtdoctor diagnosis:

```
/home/user/zephyr/drivers/serial/uart_sf32lb.c:959:1: error: '__device_dts_ord_DT_N_S_soc_S_serial_50084000_P_dmas_NAME_tx_PH_ORD' undeclared here (not in a function)
  959 | DT_INST_FOREACH_STATUS_OKAY(SF32LB_UART_DEFINE)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/drivers/serial/uart_sf32lb.c:959:1: error: 'DT_N_S_soc_S_serial_50084000_P_dmas_NAME_tx_VAL_channel' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_serial_50084000_P_dma_coherent_EXISTS'?
  959 | DT_INST_FOREACH_STATUS_OKAY(SF32LB_UART_DEFINE)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
      | DT_N_S_soc_S_serial_50084000_P_dma_coherent
/home/user/zephyr/drivers/serial/uart_sf32lb.c:959:1: error: 'DT_N_S_soc_S_serial_50084000_P_dmas_NAME_tx_VAL_request' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_serial_50084000_P_dma_coherent'?
  959 | DT_INST_FOREACH_STATUS_OKAY(SF32LB_UART_DEFINE)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
      | DT_N_S_soc_S_serial_50084000_P_dma_coherent
/home/user/zephyr/drivers/serial/uart_sf32lb.c:959:1: error: 'DT_N_S_soc_S_serial_50084000_P_dmas_NAME_tx_VAL_config' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_serial_50086000_P_clocks_IDX_0_VAL_id'?
  959 | DT_INST_FOREACH_STATUS_OKAY(SF32LB_UART_DEFINE)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
      | DT_N_S_soc_S_serial_50086000_P_clocks_IDX_0_VAL_id
/home/user/zephyr/drivers/serial/uart_sf32lb.c:959:1: error: '__device_dts_ord_DT_N_S_soc_S_serial_50084000_P_dmas_NAME_rx_PH_ORD' undeclared here (not in a function)
/home/user/zephyr/drivers/serial/uart_sf32lb.c:959:1: error: 'DT_N_S_soc_S_serial_50084000_P_dmas_NAME_rx_VAL_channel' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_serial_50084000_P_dma_coherent_EXISTS'?
  959 | DT_INST_FOREACH_STATUS_OKAY(SF32LB_UART_DEFINE)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
      | DT_N_S_soc_S_serial_50084000_P_dma_coherent_EXISTS
/home/user/zephyr/drivers/serial/uart_sf32lb.c:959:1: error: 'DT_N_S_soc_S_serial_50084000_P_dmas_NAME_rx_VAL_request' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_serial_50084000_P_dma_coherent'?
  959 | DT_INST_FOREACH_STATUS_OKAY(SF32LB_UART_DEFINE)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
      | DT_N_S_soc_S_serial_50084000_P_dma_coherent
/home/user/zephyr/drivers/serial/uart_sf32lb.c:959:1: error: 'DT_N_S_soc_S_serial_50084000_P_dmas_NAME_rx_VAL_config' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_serial_50084000_P_dma_coherent_EXISTS'?
  959 | DT_INST_FOREACH_STATUS_OKAY(SF32LB_UART_DEFINE)
      | ^~~~~~~~~~~~~~~~~~~~~~~~~~~
      | DT_N_S_soc_S_serial_50084000_P_dma_coherent_EXISTS
+-------------------------------------------------------------------------------------+
| DT Doctor                                                                           |
+=====================================================================================+
| 'usart1: /soc/serial@50084000' has no 'dmas' property.                              |
|                                                                                     |
| The node's binding declares 'dmas', but the node does not set it and the            |
| binding gives it no default value. Set it in a devicetree overlay, read it with     |
| DT_PROP_OR(), or guard the access with DT_NODE_HAS_PROP().                          |
|                                                                                     |
| Binding: /home/user/zephyr/dts/bindings/serial/sifli,sf32lb-usart.yaml              |
|                                                                                     |
| In C, property names are lowercased and '-', ',', '.', '@', '/' and '+' become '_', |
| so a 'clock-frequency' property is DT_PROP(node_id, clock_frequency).               |
+-------------------------------------------------------------------------------------+
(the same "DT Doctor" box, verbatim, repeats 8 total times in the build log — once per diagnostic emission point along the failing macro-expansion chain — before:)
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-pt2sifli-dtd
```

dtdoctor correctly identified the "property a node does not have" family
(specifically a missing `dmas` named-phandle-array property that the
`sifli,sf32lb-usart.yaml` binding declares but neither the node nor the
binding's defaults supply), and pointed at the exact binding file with
concrete remediation (`DT_PROP_OR()` / `DT_NODE_HAS_PROP()` / overlay).

### lpcxpresso55s69

- **Board:** `lpcxpresso55s69/lpc55s69/cpu0`
- **Test:** `tests/subsys/fs/littlefs` (`filesystem.littlefs.default`)

Baseline error:

```
/tmp/build-lpcxpresso55s69-base/zephyr/include/generated/zephyr/devicetree_generated.h:13051:40: error: 'DT_N_S_soc_S_peripheral_50000000_S_flash_controller_34000_S_flash_0_S_partitions_S_partition_75800_PARTITION_ID' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_peripheral_50000000_S_flash_controller_34000_S_flash_0_S_partitions_S_partition_70000_PARTITION_ID'?
13051 | #define DT_N_NODELABEL_small_partition DT_N_S_soc_S_peripheral_50000000_S_flash_controller_34000_S_flash_0_S_partitions_S_partition_75800
      |                                        ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:72:26: note: in definition of macro '__DEBRACKET'
   72 | #define __DEBRACKET(...) __VA_ARGS__
      |                          ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:64:9: note: in expansion of macro '__GET_ARG2_DEBRACKET'
   64 |         __GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)
      |         ^~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_internal.h:59:9: note: in expansion of macro '__COND_CODE'
   59 |         __COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/sys/util_macro.h:210:9: note: in expansion of macro 'Z_COND_CODE_1'
  210 |         Z_COND_CODE_1(_flag, _if_1_code, _else_code)
      |         ^~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/storage/flash_map.h:390:9: note: in expansion of macro 'COND_CODE_1'
  390 |         COND_CODE_1(DT_NODE_HAS_COMPAT(DT_NODELABEL(label), zephyr_mapped_partition),   \
      |         ^~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree/fixed-partitions.h:78:40: note: in expansion of macro 'DT_CAT'
   78 | #define DT_FIXED_PARTITION_ID(node_id) DT_CAT(node_id, _PARTITION_ID)
      |                                        ^~~~~~
/home/user/zephyr/include/zephyr/storage/flash_map.h:392:22: note: in expansion of macro 'DT_FIXED_PARTITION_ID'
  392 |                     (DT_FIXED_PARTITION_ID(DT_NODELABEL(label))))
      |                      ^~~~~~~~~~~~~~~~~~~~~
/home/user/zephyr/include/zephyr/devicetree.h:6280:24: note: in expansion of macro 'DT_N_NODELABEL_small_partition'
 6280 | #define DT_CAT(a1, a2) a1 ## a2
      |                        ^~
/home/user/zephyr/include/zephyr/devicetree.h:197:29: note: in expansion of macro 'DT_CAT'
  197 | #define DT_NODELABEL(label) DT_CAT(DT_N_NODELABEL_, label)
      |                             ^~~~~~
/home/user/zephyr/include/zephyr/storage/flash_map.h:392:44: note: in expansion of macro 'DT_NODELABEL'
  392 |                     (DT_FIXED_PARTITION_ID(DT_NODELABEL(label))))
      |                                            ^~~~~~~~~~~~
/home/user/zephyr/tests/subsys/fs/littlefs/src/testfs_lfs.c:13:33: note: in expansion of macro 'PARTITION_ID'
   13 | #define SMALL_PARTITION_ID      PARTITION_ID(SMALL_PARTITION)
      |                                 ^~~~~~~~~~~~
/home/user/zephyr/tests/subsys/fs/littlefs/src/testfs_lfs.c:25:32: note: in expansion of macro 'SMALL_PARTITION_ID'
   25 |         .storage_dev = (void *)SMALL_PARTITION_ID,
      |                                ^~~~~~~~~~~~~~~~~~
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-lpcxpresso55s69-base
```

dtdoctor diagnosis:

```
/home/user/zephyr/tests/subsys/fs/littlefs/src/testfs_lfs.c:25:32: error: 'DT_N_S_soc_S_peripheral_50000000_S_flash_controller_34000_S_flash_0_S_partitions_S_partition_75800_PARTITION_ID' undeclared here (not in a function); did you mean 'DT_N_S_soc_S_peripheral_50000000_S_flash_controller_34000_S_flash_0_S_partitions_S_partition_70000_PARTITION_ID'?
   25 |         .storage_dev = (void *)SMALL_PARTITION_ID,
      |                                ^~~~~~~~~~~~~~~~~~
      |                                DT_N_S_soc_S_peripheral_50000000_S_flash_controller_34000_S_flash_0_S_partitions_S_partition_70000_PARTITION_ID
+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| DT Doctor                                                                                                                                                                                                                                                     |
+===============================================================================================================================================================================================================================================================+
| 'small_partition: /soc/peripheral@50000000/flash-controller@34000/flash@0/partitions/partition@75800' exists, but 'DT_N_S_soc_S_peripheral_50000000_S_flash_controller_34000_S_flash_0_S_partitions_S_partition_75800_PARTITION_ID' was not generated for it. |
|                                                                                                                                                                                                                                                               |
| The devicetree API asked for 'PARTITION_ID' on this node, and nothing in the                                                                                                                                                                                  |
| devicetree provides it. This is usually an out-of-range index, or a register,                                                                                                                                                                                 |
| interrupt or phandle cell the node does not define.                                                                                                                                                                                                           |
|                                                                                                                                                                                                                                                               |
| Search for 'DT_N_S_soc_S_peripheral_50000000_S_flash_controller_34000_S_flash_0_S_partitions_S_partition_75800_' in                                                                                                                                           |
| <build>/zephyr/include/generated/zephyr/devicetree_generated.h to see which macros do                                                                                                                                                                         |
| exist for this node.                                                                                                                                                                                                                                          |
+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-lpcxpresso55s69-dtd
```

The node itself (`small_partition`) exists in devicetree, but the specific
generated macro the code asked for (`PARTITION_ID`, gated on the node having
compat `zephyr,mapped-partition`) was never generated for it — a
"node exists but the requested generated macro/property was not produced for
it" case, distinct from the plain node-label-missing family, and dtdoctor's
box states that distinction explicitly rather than claiming the node itself
is absent.

### ch32h417evt

- **Board:** `ch32h417evt/ch32h417/v3f`
- **Test:** `tests/kernel/timer/timer_behavior` (`kernel.timer.timer`)

Baseline error:

```
FAILED: zephyr/zephyr_pre0.elf zephyr/zephyr_pre0.map /tmp/build-ch32h417evt-base/zephyr/zephyr_pre0.map 
: && /home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gcc  -Os zephyr/CMakeFiles/zephyr_pre0.dir/misc/empty_file.c.obj -o zephyr/zephyr_pre0.elf  zephyr/CMakeFiles/offsets.dir/./arch/riscv/core/offsets/offsets.c.obj  -T  zephyr/linker_zephyr_pre0.cmd  -Wl,-Map,/tmp/build-ch32h417evt-base/zephyr/zephyr_pre0.map  -Wl,--whole-archive  app/libapp.a  zephyr/libzephyr.a  zephyr/arch/common/libarch__common.a  zephyr/arch/arch/riscv/core/libarch__riscv__core.a  zephyr/lib/libc/picolibc/liblib__libc__picolibc.a  zephyr/lib/libc/common/liblib__libc__common.a  zephyr/subsys/portability/posix/c_lib_ext/libsubsys__portability__posix__c_lib_ext.a  zephyr/subsys/testsuite/ztest/libsubsys__testsuite__ztest.a  zephyr/drivers/interrupt_controller/libdrivers__interrupt_controller.a  zephyr/drivers/gpio/libdrivers__gpio.a  zephyr/drivers/timer/libdrivers__timer.a  -Wl,--no-whole-archive  zephyr/kernel/libkernel.a  -L/tmp/build-ch32h417evt-base/zephyr  zephyr/arch/common/libisr_tables.a  -fuse-ld=bfd  -mabi=ilp32  -march=rv32imac_zicsr_zifencei  -mcmodel=medlow  -Wl,--gc-sections  -Wl,--build-id=none  -Wl,--sort-common=descending  -Wl,--sort-section=alignment  -Wl,-u,_OffsetAbsSyms  -Wl,-u,_ConfigAbsSyms  -nostdlib  -static  -znoexecstack  -Wl,-X  -Wl,-N  -Wl,--orphan-handling=warn  -Wl,-no-pie  -Wl,--undefined=_sw_isr_table  -Wl,--undefined=_irq_vector_table  -specs=picolibc.specs  -DPICOLIBC_DOUBLE_PRINTF_SCANF -L"/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/../lib/gcc/riscv64-zephyr-elf/14.3.0/rv32imac_zicsr_zifencei/ilp32/space" -lc -lgcc && cd /tmp/build-ch32h417evt-base/zephyr && /usr/bin/cmake -E true
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/../lib/gcc/riscv64-zephyr-elf/14.3.0/../../../../riscv64-zephyr-elf/bin/ld.bfd: zephyr/drivers/gpio/libdrivers__gpio.a(wch_gpio_ch32v00x.c.obj):(.rodata.gpio_ch32v00x_1_config+0x8): undefined reference to `__device_dts_ord_14'
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/../lib/gcc/riscv64-zephyr-elf/14.3.0/../../../../riscv64-zephyr-elf/bin/ld.bfd: zephyr/drivers/gpio/libdrivers__gpio.a(wch_gpio_ch32v00x.c.obj):(.rodata.gpio_ch32v00x_0_config+0x8): undefined reference to `__device_dts_ord_14'
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-ch32h417evt-base
```

dtdoctor diagnosis:

```
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/../lib/gcc/riscv64-zephyr-elf/14.3.0/../../../../riscv64-zephyr-elf/bin/ld.bfd: zephyr/drivers/gpio/libdrivers__gpio.a(wch_gpio_ch32v00x.c.obj):(.rodata.gpio_ch32v00x_1_config+0x8): undefined reference to `__device_dts_ord_14'
/home/user/zephyr-sdk/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/../lib/gcc/riscv64-zephyr-elf/14.3.0/../../../../riscv64-zephyr-elf/bin/ld.bfd: zephyr/drivers/gpio/libdrivers__gpio.a(wch_gpio_ch32v00x.c.obj):(.rodata.gpio_ch32v00x_0_config+0x8): undefined reference to `__device_dts_ord_14'
collect2: error: ld returned 1 exit status
+-----------------------------------------------------------------------------------+
| DT Doctor                                                                         |
+===================================================================================+
| 'rcc: /soc/rcc@40021000' is enabled but no driver appears to be available for it. |
|                                                                                   |
| Try enabling these Kconfig options:                                               |
|                                                                                   |
|  - CONFIG_CLOCK_CONTROL=y                                                         |
+-----------------------------------------------------------------------------------+
ninja: build stopped: subcommand failed.
FATAL ERROR: command exited with status 1: /usr/bin/cmake --build /tmp/build-ch32h417evt-dtd
```

Another "enabled but no driver" case, this time surfaced at GPIO driver link
time (two GPIO controller instances both reference the clock-control `rcc`
node via `__device_dts_ord_14`, which has no bound driver), with
`CONFIG_CLOCK_CONTROL=y` suggested as the fix.

## 3. Blocked cases

None. Every item attempted this sweep reached a `matched` or `not-a-dt-issue`
verdict; no item was abandoned due to missing HAL modules, missing shield
configs, or other unresolved infrastructure blockers.

## 4. Not-a-dt-issue cases

- **heltec_t114_v2** (`heltec_t114_v2/nrf52840`, `tests/drivers/build_all/led_strip`) — the baseline failure is `error: 'drv_data' undeclared (first use in this function)` in `drivers/led_strip/ws2812_gpio.c`'s `send_buf()`. This is a plain C typo/bug: the function already has a correctly named local `clk_dev` obtained via `DEVICE_DT_GET_ONE`, but the `CONFIG_CLOCK_CONTROL_NRF`-undefined branch references a non-existent `drv_data->clk_dev` instead. No devicetree-generated macro is leaked or undeclared here, so no dtdoctor build was run for it.

## 5. Coverage assessment

Diagnosis families from PR #501, and whether real CI failures in this sweep
exercised them:

- **enabled-node-no-driver** ("`X` is enabled but no driver appears to be
  available for it"): exercised heavily — `kit_t2g_b_h_evk`, `beaglebadge`,
  `lp_em_cc2340r5`, `96b_wistrio`, `adafruit_feather_rfm95_rp2040`,
  `bytesensi_l`, `rm1xx_dvk`, `ttgo_lora32`, `ttgo_tbeam`, `ch32h417evt` — 10
  of the 18 matched cases, both at compile time (leaked child-node macros in
  flash/GPIO drivers) and at link time (`__device_dts_ord_N` undefined
  references from LoRa/power-domain nodes).
- **node-identifier-naming-no-node** ("No node label 'X' exists in this
  build's devicetree"): exercised by `ek_ra2a1`, `heltec_wifi_lora32_v2`, and
  `esp32p4x_spi` (the latter hitting it twice, for two separate missing
  labels).
- **disabled-ancestor walking** ("'X' is disabled in <file>:<line>", with
  dependent-node and alias listing where applicable): exercised by
  `scobc_v1`, `esp32p4x_lp_uart`, and `serpente`. `serpente` is the fullest
  exercise of the dependency walk, listing two dependent nodes and an alias;
  `esp32p4x_lp_uart` shows the simplest form with no dependents.
- **property-a-node-does-not-have** ("'X' has no 'Y' property"): exercised by
  `pt2_sifli` (missing `dmas` named-phandle-array property against a binding
  that declares it with no default).
- **node-exists-but-macro-not-generated** (a close cousin of the property
  family — "'X' exists, but 'MACRO' was not generated for it"): exercised by
  `lpcxpresso55s69` (`PARTITION_ID` not generated for a partition node
  because it isn't `zephyr,mapped-partition` compatible).

Not exercised by any real CI failure in this sweep: a standalone
**specifier-cell/index/name** mismatch (e.g., an out-of-range `#-cells` index
or unknown named cell against an otherwise-present property) distinct from
the "property does not exist at all" and "macro not generated" cases above,
and a standalone **DT_INST/DT_DRV_COMPAT** diagnosis (e.g., `DT_INST_FOREACH`
over zero compatible-matching instances) not already folded into the
enabled-node-no-driver or disabled-ancestor cases above. Both remain
plausible dtdoctor diagnosis paths per PR #501 but were not triggered by any
of the 19 failures attempted in this sweep.
