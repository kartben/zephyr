# zenbedded-demo

A single-axis, gravity-loaded robot joint. Zephyr runs the control loop at 1 kHz on the
MCU. The joint appears in a ROS 2 graph as `ros2_control` hardware over Zenoh. When the
companion computer dies mid-motion, the MCU degrades safely on its own and resumes
without a jerk when the host returns.

The claim this demo exists to support: **the control loop's correctness does not depend
on the host being alive.**

See [`../SPEC.md`](../SPEC.md) for the full build specification.

## One piece of honesty, up front

The MCU speaks **plain Zenoh with a documented key scheme and a packed binary payload**,
not the `rmw_zenoh` wire format. Translation to ROS types happens inside the
`ros2_control` hardware interface, which is a plugin loaded into the `controller_manager`
process.

So this is **not agent-free** in the strict sense. It is **daemon-free**: there is no
separate bridge process per device, and no XRCE-DDS agent to deploy, supervise or version-
match. That is the real, defensible win, and it is the one worth claiming at a booth.

`rmw_zenoh_pico` (eSOL) is the path to true RMW-level parity. It is out of scope here, and
noted as future work.

## Status

| Phase | Scope | State |
|---|---|---|
| 0 | Skeleton, west manifest, both targets build | done |
| 1 | Control core, simulated plant, no networking | done |
| 2 | Zenoh transport | not started |
| 3 | `ros2_control` hardware interface | not started |
| 4 | Hardware bring-up on `nucleo_f767zi` | not started |
| 5 | Dashboard and jitter comparison | not started |
| 6 | CI | not started |

## Layout

```
zenbedded-demo/
  west.yml              standalone workspace manifest (zephyr + zenoh-pico)
  zephyr/module.yml     makes this directory a Zephyr module (dts_root + Kconfig)
  Kconfig               control-core configuration
  CMakeLists.txt        control-core library
  include/zenbedded/    public headers
  lib/                  control core: joint table, PID, failover, histogram, plant
  app/                  firmware application
  tests/                twister tests
  dts/bindings/         zenbedded,robot and zenbedded,revolute-joint
```

## Building

This repository is a Zephyr fork, so the surrounding tree is already the west workspace
and the app locates this directory through `ZEPHYR_EXTRA_MODULES` (see
`app/CMakeLists.txt`). Nothing extra to configure:

```sh
west build -b native_sim      zenbedded-demo/app
west build -b nucleo_f767zi   zenbedded-demo/app
```

Standalone (outside a Zephyr fork), `west.yml` is the manifest:

```sh
west init -m https://github.com/<org>/zenbedded-demo --mr main zb-ws
cd zb-ws && west update
west build -b native_sim zenbedded-demo/app
```

## Tests

```sh
west twister -T zenbedded-demo/tests -p native_sim
```

The headline test is `tests/resume`, which drives the simulated plant through the full
`FOLLOWING → PARK → resume` cycle and asserts that no control-output discontinuity exceeds
a threshold. If that test is failing, the demo does not work, whatever else is green.

## Devicetree contract

Joint topology, limits, gains and (from Phase 2) the Zenoh key expressions all derive from
devicetree. Adding a second joint is a DT edit plus a rebuild, with zero C changes — the
joint table is built at compile time with `DT_FOREACH_CHILD_STATUS_OKAY` and lives in ROM.

```dts
robot {
    compatible = "zenbedded,robot";
    robot-id = "arm0";

    joint0 {
        compatible = "zenbedded,revolute-joint";
        joint-name = "shoulder";
        limit-lo-mrad = <(-1570)>;
        limit-hi-mrad = <1570>;
        max-effort-mnm = <400>;
        hold-effort-mnm = <150>;
        pid-kp-milli = <1200>;
        pid-ki-milli = <80>;
        pid-kd-milli = <45>;
    };
};
```

## Instrumentation

Loop period is binned **on-device**. Publishing per-sample timestamps at 1 kHz would inject
the very jitter the demo is measuring. `timing_counter_get()` is sampled at the top of each
iteration, the delta goes into a fixed-size RAM histogram with log-ish buckets around the
1000 µs target, and p50 / p99 / p99.9 / max are reported together — never max alone.
Overruns are counted separately, sourced from the kernel timer's own missed-tick count.
