# dtdoctor CI Demo: `debug.coredump.drivers.api`

## 1. Summary

This report documents a real, hands-on reproduction of a devicetree-related
build failure taken from Zephyr's weekly CI (twister) run, and shows that
`dtdoctor` correctly diagnoses it end to end.

The failing test is `debug.coredump.drivers.api`
(`tests/drivers/coredump/coredump_api`), which fails to build for platforms
`qemu_riscv32/qemu_virt_riscv32/aia-direct` and its `/smp` variant. This
failure appears in the "Build failure" bucket of the twister
`twister_report_summary.json` weekly CI report. The exact undeclared-macro
signature seen in that report,
`DT_N_S_coredump_device0_P_memory_regions_IDX_0`, matches the error
reproduced here.

The build was performed for real — not simulated or run against a synthetic
fixture — using the actual Zephyr SDK v1.0.1 `riscv64-zephyr-elf` toolchain
against a bare `west` workspace and the real `tests/drivers/coredump/coredump_api`
test sources and overlays from this checkout. `dtdoctor` was invoked as the
build's static-code-analysis (SCA) variant and correctly diagnosed the root
cause of the failure from the raw macro-soup GCC error, naming the exact
missing node label and missing property, the binding file involved, and a
suggested fix.

## 2. Branches combined for this demo

Getting a `dtdoctor` capable of diagnosing this exact failure required
merging three branches of `kartben/zephyr`:

- **`main`** — ships the original `dtdoctor`, which only understands a
  single family of leaked devicetree macros (the `__device_dts_ord_` shape).
- **`dtdoctor-tests`** — test scaffolding for `dtdoctor`.
- **PR #501** (`claude/error-macro-reverse-engineer-t29qdc`) — adds two new
  diagnosis families: "property a node does not have" and "node identifier
  naming no node". Both of these families are exercised by this exact
  failure (the missing `coredump_device1` label and the missing
  `memory_regions` property on `coredump_device0`).
- **`sca-launcher-ccache`** — fixes the SCA compiler-launcher `CACHE
  INTERNAL` clobbering bug so that `dtdoctor` chains correctly with other
  compiler launchers (e.g. `ccache`), as is the case in the real CI
  environment.

It is worth stating plainly: **without PR #501, `dtdoctor` would not have
recognized this failure at all.** Before that PR, `dtdoctor` only understood
the `__device_dts_ord_` macro shape, and none of the macros leaked by this
failure (`DT_N_S_coredump_device0_P_memory_regions_IDX_0`,
`DT_N_NODELABEL_coredump_device1_P_memory_regions_IDX_0`, etc.) fit that
shape.

## 3. Without dtdoctor: the raw CI experience

This is the raw compiler error as it appears in the baseline (non-dtdoctor)
build — exactly the kind of macro-soup GCC error a developer sees in the
current weekly CI log today, with no indication of which devicetree node or
property is actually missing:

```
/home/user/zephyr/tests/drivers/coredump/coredump_api/src/main.c: In function 'coredump_tests_suite_setup':
/home/user/zephyr/tests/drivers/coredump/coredump_api/src/main.c:64:29: error: 'DT_N_S_coredump_device0_P_memory_regions_IDX_0' undeclared (first use in this function); did you mean 'DT_N_S_coredump_device_cb_P_memory_regions_IDX_0'?
   64 |                 (uint32_t *)DT_PROP_BY_IDX(DT_NODELABEL(coredump_device0), memory_regions, 0);
      |                             ^~~~~~~~~~~~~~
      |                             DT_N_S_coredump_device_cb_P_memory_regions_IDX_0
/home/user/zephyr/tests/drivers/coredump/coredump_api/src/main.c:64:29: note: each undeclared identifier is reported only once for each function it appears in
/home/user/zephyr/tests/drivers/coredump/coredump_api/src/main.c:66:29: error: 'DT_N_S_coredump_device0_P_memory_regions_IDX_2' undeclared (first use in this function); did you mean 'DT_N_S_coredump_device_cb_P_memory_regions_IDX_0'?
   66 |                 (uint32_t *)DT_PROP_BY_IDX(DT_NODELABEL(coredump_device0), memory_regions, 2);
      |                             ^~~~~~~~~~~~~~
      |                             DT_N_S_coredump_device_cb_P_memory_regions_IDX_0
/home/user/zephyr/tests/drivers/coredump/coredump_api/src/main.c:68:29: error: 'DT_N_NODELABEL_coredump_device1_P_memory_regions_IDX_0' undeclared (first use in this function); did you mean 'DT_N_S_coredump_device_cb_P_memory_regions_IDX_0'?
   68 |                 (uint32_t *)DT_PROP_BY_IDX(DT_NODELABEL(coredump_device1), memory_regions, 0);
      |                             ^~~~~~~~~~~~~~
      |                             DT_N_S_coredump_device_cb_P_memory_regions_IDX_0
```

A developer reading only this output has to reverse-engineer, by hand, what
`DT_N_S_coredump_device0_P_memory_regions_IDX_0` and
`DT_N_NODELABEL_coredump_device1_P_memory_regions_IDX_0` are supposed to mean
in devicetree terms, and GCC's "did you mean" suggestions point at an
unrelated macro (`coredump_device_cb`'s), which is actively misleading.

## 4. With dtdoctor: the raw diagnosis

### qemu_riscv32/qemu_virt_riscv32/aia-direct build

```
+-----------------------------------------------------------------------------------+
| DT Doctor                                                                         |
+===================================================================================+
| No node label 'coredump_device1' exists in this build's devicetree.               |
|                                                                                   |
| Did you mean one of these?                                                        |
|                                                                                   |
|  - coredump_device0                                                               |
|  - coredump_devicecb                                                              |
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
+-------------------------------------------------------------------------------------+
| DT Doctor                                                                           |
+=====================================================================================+
| 'coredump_device0: /coredump-device0' has no 'memory_regions' property.             |
|                                                                                     |
| The node's binding declares 'memory-regions', but the node does not set it and the  |
| binding gives it no default value. Set it in a devicetree overlay, read it with     |
| DT_PROP_OR(), or guard the access with DT_NODE_HAS_PROP().                          |
|                                                                                     |
| Binding: /home/user/zephyr/dts/bindings/coredump/zephyr,coredump.yaml               |
|                                                                                     |
| In C, property names are lowercased and '-', ',', '.', '@', '/' and '+' become '_', |
| so a 'clock-frequency' property is DT_PROP(node_id, clock_frequency).               |
+-------------------------------------------------------------------------------------+
+-------------------------------------------------------------------------------------+
| DT Doctor                                                                           |
+=====================================================================================+
| 'coredump_device0: /coredump-device0' has no 'memory_regions' property.             |
|                                                                                     |
| The node's binding declares 'memory-regions', but the node does not set it and the  |
| binding gives it no default value. Set it in a devicetree overlay, read it with     |
| DT_PROP_OR(), or guard the access with DT_NODE_HAS_PROP().                          |
|                                                                                     |
| Binding: /home/user/zephyr/dts/bindings/coredump/zephyr,coredump.yaml               |
|                                                                                     |
| In C, property names are lowercased and '-', ',', '.', '@', '/' and '+' become '_', |
| so a 'clock-frequency' property is DT_PROP(node_id, clock_frequency).               |
+-------------------------------------------------------------------------------------+
```

### /smp variant (included to show the diagnosis reproduces identically)

```
+-----------------------------------------------------------------------------------+
| DT Doctor                                                                         |
+===================================================================================+
| No node label 'coredump_device1' exists in this build's devicetree.               |
|                                                                                   |
| Did you mean one of these?                                                        |
|                                                                                   |
|  - coredump_device0                                                               |
|  - coredump_devicecb                                                              |
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
+-------------------------------------------------------------------------------------+
| DT Doctor                                                                           |
+=====================================================================================+
| 'coredump_device0: /coredump-device0' has no 'memory_regions' property.             |
|                                                                                     |
| The node's binding declares 'memory-regions', but the node does not set it and the  |
| binding gives it no default value. Set it in a devicetree overlay, read it with     |
| DT_PROP_OR(), or guard the access with DT_NODE_HAS_PROP().                          |
|                                                                                     |
| Binding: /home/user/zephyr/dts/bindings/coredump/zephyr,coredump.yaml               |
|                                                                                     |
| In C, property names are lowercased and '-', ',', '.', '@', '/' and '+' become '_', |
| so a 'clock-frequency' property is DT_PROP(node_id, clock_frequency).               |
+-------------------------------------------------------------------------------------+
+-------------------------------------------------------------------------------------+
| DT Doctor                                                                           |
+=====================================================================================+
| 'coredump_device0: /coredump-device0' has no 'memory_regions' property.             |
|                                                                                     |
| The node's binding declares 'memory-regions', but the node does not set it and the  |
| binding gives it no default value. Set it in a devicetree overlay, read it with     |
| DT_PROP_OR(), or guard the access with DT_NODE_HAS_PROP().                          |
|                                                                                     |
| Binding: /home/user/zephyr/dts/bindings/coredump/zephyr,coredump.yaml               |
|                                                                                     |
| In C, property names are lowercased and '-', ',', '.', '@', '/' and '+' become '_', |
| so a 'clock-frequency' property is DT_PROP(node_id, clock_frequency).               |
+-------------------------------------------------------------------------------------+
```

`dtdoctor` emitted one box per distinct leaked macro:

- One **"node label does not exist"** diagnosis, for `coredump_device1`,
  which also suggests the two node labels that do exist
  (`coredump_device0`, `coredump_devicecb`).
- Two **"property a node does not have"** diagnoses (one per leaked
  `IDX_0`/`IDX_2` macro), both for `coredump_device0`'s missing
  `memory_regions` property, each naming the exact node path
  (`/coredump-device0`), the property, and the binding file
  (`/home/user/zephyr/dts/bindings/coredump/zephyr,coredump.yaml`), along
  with a suggested fix (`DT_PROP_OR()` or `DT_NODE_HAS_PROP()`).

Both the qemu_riscv32/qemu_virt_riscv32/aia-direct build and its `/smp`
variant produce byte-for-byte identical diagnosis boxes, confirming the
diagnosis is stable across both failing CI platform entries.

## 5. Root cause dtdoctor pointed us to

Following the diagnosis boxes to the actual devicetree sources shows why the
nodes are missing/incomplete.

The application-level overlay that ships with the test
(`app.overlay`) only defines `coredump_device0`, and without a
`memory-regions` property:

```
coredump_device0: coredump-device0 {
	compatible = "zephyr,coredump";
	coredump-type = "COREDUMP_TYPE_MEMCPY";
	status = "okay";
};
```

But the test also ships a richer, board-specific overlay
(`tests/drivers/coredump/coredump_api/boards/qemu_riscv32.overlay`) that
defines all three nodes the test source references, including
`coredump_device1` and a `memory-regions` property on `coredump_device0`:

```
coredump_device0: coredump-device0 {
	compatible = "zephyr,coredump";
	coredump-type = "COREDUMP_TYPE_MEMCPY";
	status = "okay";

	memory-regions = <0x85000000 0x4>,
			 <0x85000004 0x4>;
};

coredump_device1: coredump-device1 {
	compatible = "zephyr,coredump";
	coredump-type = "COREDUMP_TYPE_MEMCPY";
	status = "okay";

	memory-regions = <0x86000000 0xc>;
};

coredump_devicecb: coredump-device-cb {
	compatible = "zephyr,coredump";
	coredump-type = "COREDUMP_TYPE_CALLBACK";
	status = "okay";
	memory-regions = <0x0 0x4>;
};
```

This board overlay is never actually applied to the
`qemu_riscv32/qemu_virt_riscv32/aia-direct` build target. Zephyr's
board-overlay auto-inclusion matches an overlay's filename against the exact
board+qualifiers target being built. The file is named `qemu_riscv32.overlay`
— pre-hwmv2 naming — and that name does not match the hwmv2-style target
`qemu_riscv32/qemu_virt_riscv32/aia-direct`. As a result, the build silently
falls back to the generic `app.overlay`, which under-specifies both nodes:
`coredump_device1` doesn't exist at all in the devicetree actually used for
this build, and `coredump_device0` exists but has no `memory-regions`
property. This matches the binding: the `zephyr,coredump.yaml` binding
declares `memory-regions` as an optional array-type property — it is not
`required: true` and has no default value — so a node using this compatible
is not required to set it, and the binding itself gives no error when it's
left unset; the error only surfaces later, in C, as an undeclared macro.

This is the actual bug behind the CI failure, and it is a **devicetree
overlay-selection bug**, not a `dtdoctor` bug. `dtdoctor`'s job here was to
make that overlay-selection gap immediately legible — naming the missing
node and property directly — instead of leaving a bare, unexplained
"undeclared identifier" for a developer to chase by hand.

## 6. How this was run

- Toolchain: Zephyr SDK v1.0.1, `riscv64-zephyr-elf` toolchain only.
- Workspace: a bare `west` workspace (no HAL modules were needed for this
  target).
- Build command:
  ```
  west build -b qemu_riscv32/qemu_virt_riscv32/aia-direct -- -DZEPHYR_SCA_VARIANT=dtdoctor
  ```
  run against `tests/drivers/coredump/coredump_api`.
- Branch: the merged branch `claude/dtdoctor-ci-workflow-xobhn4` (combining
  `main`, `dtdoctor-tests`, PR #501, and `sca-launcher-ccache` as described
  in Section 2).

One rough edge was encountered and is worth flagging upstream: `dtdoctor`'s
analyzer imports `tabulate`, which is not listed in
`scripts/requirements-base.txt`. It had to be `pip install`ed separately
before the SCA variant would run. This is a small documentation/requirements
gap worth fixing in a follow-up.
