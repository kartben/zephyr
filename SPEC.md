# zenbedded-demo — build spec

Drop this at the repo root as `SPEC.md`. Kick the agent off with:

> Read `SPEC.md`. Implement Phase 0 and Phase 1 only. Stop at the Phase 1 acceptance
> criteria and show me the twister output. Do not start Phase 2.

Phase-at-a-time is not optional. One-shotting this produces firmware that builds and
does nothing useful.

---

## What we are building

A single-axis, gravity-loaded robot joint. Zephyr runs the control loop at 1 kHz on the
MCU. The joint appears in a ROS 2 graph as `ros2_control` hardware over Zenoh. When the
companion computer dies mid-motion, the MCU degrades safely on its own and resumes
without a jerk when the host returns.

The demo's whole claim is: **the control loop's correctness does not depend on the host
being alive.** Every design decision below serves that claim. If an implementation choice
weakens it, the choice is wrong.

## Architecture, and one piece of honesty

```
  Zephyr MCU                    Companion computer
  ┌────────────────┐            ┌──────────────────────────────┐
  │ control thread │            │ controller_manager           │
  │   1 kHz, coop  │            │  └ zenbedded_hardware_iface  │
  │        ▲       │  Zenoh     │ zenohd (router)              │
  │  lock-free swap│◄──────────►│ ros2_control controllers     │
  │        ▼       │  Ethernet  │ dashboard                    │
  │ comms thread   │            └──────────────────────────────┘
  │   preemptible  │
  └────────────────┘
```

The MCU speaks **plain Zenoh with a documented key scheme and a packed binary payload**,
not the `rmw_zenoh` wire format. The translation to ROS types happens inside the
`ros2_control` hardware interface, which is a plugin in the `controller_manager` process.

Say this plainly in the README. It is not agent-free in the strict sense; it is
**daemon-free**. There is no separate bridge process per device, and no XRCE-DDS agent.
That is the real, defensible win. Do not oversell it, someone at the booth will ask.

`rmw_zenoh_pico` (eSOL) is the path to true RMW-level parity. Out of scope here. Note it
in the README as future work.

---

## Hard constraints

Violating any of these breaks the demo's premise. Flag and ask rather than working around.

**Control thread**
- Cooperative priority, woken by a hardware timer, not `k_sleep()`.
- No heap. No `k_malloc`, no `malloc`, none transitively.
- No logging, no `printk`, no shell calls. Not even at `LOG_LEVEL_DBG`.
- No mutex, semaphore, or any object the comms thread can also hold. The only
  cross-thread channel is the lock-free setpoint swap.
- No network stack calls, directly or indirectly.
- Bounded worst case. Every loop path must be inspectable and constant-time.

**Setpoint handoff**
- Single-producer (comms), single-consumer (control).
- Swap a whole `struct zb_setpoint` atomically via pointer swap on a double buffer, or
  seqlock. Never a field-by-field copy the control thread can read mid-write.
- Carries `seq`, target, and the device-time cycle count at which it arrived.

**Comms thread**
- Preemptible, strictly lower priority than control.
- Owns the zenoh-pico session, all sockets, all allocation.
- May block, may die, may be starved. Control must not care.

**Build**
- Every phase must build for `native_sim` AND the hardware target. If a change only
  builds on one, it is not done.
- All configuration through Kconfig and devicetree. No magic numbers in `.c` files.
- Twister must pass before a phase is considered complete.

---

## Devicetree contract

This is the part that makes the demo *about Zephyr*. The joint topology, limits, gains,
and Zenoh key expressions all derive from DT. Adding a second joint must be a DT edit
plus a rebuild, with zero C changes.

```dts
/ {
    robot: robot {
        compatible = "zenbedded,robot";
        robot-id = "arm0";

        joint0: joint0 {
            compatible = "zenbedded,revolute-joint";
            joint-name = "shoulder";
            status = "okay";

            motor       = <&motor0>;      /* PWM + dir, or FOC driver phandle */
            encoder     = <&qdec0>;       /* sensor node, SPI or timer qdec   */

            limit-lo-mrad   = <(-1570)>;
            limit-hi-mrad   = <1570>;
            max-effort-mnm  = <400>;
            hold-effort-mnm = <150>;      /* HOLDING state cap */

            pid-kp-milli = <1200>;
            pid-ki-milli = <80>;
            pid-kd-milli = <45>;
        };
    };
};
```

**Firmware side:** iterate with `DT_FOREACH_CHILD_STATUS_OKAY` into a const joint table
in ROM. No runtime discovery. Write the binding YAMLs under `dts/bindings/zenbedded/`.

**Host side:** a build step parses the same DT with `edtlib` and emits
`ros2_control/joints.yaml` plus a URDF `<ros2_control>` fragment. Wire it as a CMake
post-build target so the ROS config can never drift from the firmware. This codegen is a
headline feature, not a convenience. Make it clean enough to demo on its own.

---

## Wire protocol

Keys, all derived from DT:

```
zb/<robot-id>/<joint-name>/cmd      host  -> mcu,  on change, ~100 Hz max
zb/<robot-id>/<joint-name>/state    mcu   -> host, 100 Hz
zb/<robot-id>/diag/loop             mcu   -> host, 1 Hz  (histogram)
zb/<robot-id>/diag/mode             mcu   -> host, on transition + 1 Hz heartbeat
```

Payloads are packed little-endian structs in a shared header used verbatim by both the
firmware and the hardware interface. Version byte first, reject on mismatch. No JSON, no
CDR, no protobuf. Milli-units on the wire, float internally.

```c
struct zb_cmd {
    uint8_t  ver;               /* ZB_PROTO_VER */
    uint8_t  flags;
    uint16_t seq;
    int32_t  position_mrad;
    int32_t  velocity_mrad_s;
    uint32_t host_us;
} __packed;

struct zb_state {
    uint8_t  ver;
    uint8_t  mode;              /* enum zb_mode */
    uint16_t seq_echo;
    int32_t  position_mrad;
    int32_t  velocity_mrad_s;
    int32_t  effort_mnm;
    uint32_t age_us;            /* since last accepted cmd */
    uint32_t dev_us;
} __packed;
```

Add a `static_assert` on both struct sizes. It will save you an afternoon.

---

## Failover state machine

Owned entirely by the MCU. It never waits for the host to declare a problem. The clock
keys off **command arrival**, not link state, so a wedged host holding TCP open but
publishing nothing is caught the same as an unplugged cable.

| Age of last valid cmd | Mode | Behavior |
|---|---|---|
| < 30 ms | `FOLLOWING` | track commanded position, full gains |
| 30 ms – 500 ms | `DEGRADED` | hold last valid setpoint, full gains |
| 500 ms – 3 s | `HOLDING` | hold, effort clamped to `hold-effort-mnm` |
| > 3 s | `PARK` | rate-limited descent to rest, then driver disable |

Thresholds are Kconfig, defaults above.

**Hardware watchdog:** arm `drivers/watchdog.h`, fed only from the control thread. An MCU
hang parks the motor too. Wire the driver enable pin so `PARK` genuinely coasts.

**Bumpless resume.** This is the part everyone skips and it is where the demo bites you.
On reactivation, `ros2_control` will push whatever stale command sat in its buffer and the
arm will slam.

1. Hardware interface `on_activate()` seeds its command buffer from the MCU's *reported*
   position before the first write. Never from a cached or default value.
2. The MCU refuses to leave `DEGRADED`/`HOLDING` until it receives a command within
   `ZB_RESUME_EPSILON_MRAD` of its current position.
3. Once accepted, ramp gains over `ZB_RESUME_RAMP_MS` back to full.

A visibly smooth resume is worth more to a robotics audience than the kill itself.

---

## Instrumentation

Bin on-device. Publishing per-sample timestamps at 1 kHz injects the jitter you are
trying to measure, and someone in the audience will point that out.

- `CONFIG_TIMING_FUNCTIONS`, `timing_counter_get()` at the top of each iteration.
- Delta into a fixed-size histogram in RAM. Log-ish buckets around the 1000 µs target.
- Publish the whole histogram at 1 Hz on `diag/loop`.
- Report p50 / p99 / p99.9 / max. Never max alone.
- Track and publish overrun count separately.

State the instrumentation method on the slide.

---

## Phases

Each phase ends with the listed commands passing. Do not proceed past a phase without
showing output.

### Phase 0 — skeleton
West manifest (zephyr + zenoh-pico as a submanifest), repo layout, empty app that builds
for `native_sim` and `nucleo_f767zi`, twister config, pre-commit with clang-format using
Zephyr's style.

**Accept:** `west build -b native_sim app` and `west build -b nucleo_f767zi app` both
clean.

### Phase 1 — control core, no networking
Joint table from DT. Control thread on a timer. PID. Failover state machine. Lock-free
setpoint handoff. Histogram. A **simulated plant** behind the same internal API as the
real motor/encoder, selected by Kconfig, so the whole thing is testable headless: second
order, gravity term, configurable inertia and damping.

Twister tests: loop period holds under `native_sim`; every state transition fires at the
right threshold; `PARK` descent is rate-limited; setpoint handoff survives a fuzzed
producer; resume epsilon rejects a stale far-away command.

**Accept:** `west twister -T tests/ -p native_sim` green, and a test that drives the plant
through the full FOLLOWING → PARK → resume cycle asserting no discontinuity above a
threshold. This is the most important test in the repo.

### Phase 2 — Zenoh transport
zenoh-pico session in the comms thread. Publish/subscribe on the DT-derived keys. Shared
protocol header. A throwaway Python probe using `zenoh-python` to drive the joint and dump
state, so Phase 2 is verifiable without any ROS installed.

**Accept:** `native_sim` binary + local `zenohd` + Python probe moves the simulated joint;
killing the probe drives the state machine to `PARK`; restarting it resumes smoothly.

### Phase 3 — ros2_control
`zenbedded_hardware_interface` as a `SystemInterface` plugin. `edtlib` codegen for
`joints.yaml` and the URDF fragment. `on_activate()` seeding per the bumpless rules.
A launch file bringing up `controller_manager` with a `joint_trajectory_controller`.

**Accept:** `ros2 topic list` shows the joint. `ros2 control list_hardware_interfaces`
shows it active. A trajectory command moves the simulated joint. `docker compose up`
brings up the whole graph against `native_sim` with no hardware.

### Phase 4 — hardware
`nucleo_f767zi` overlay. Real PWM + `qdec`. Ethernet. Watchdog. Current limit. Calibration
routine for the encoder zero.

**Accept:** same trajectory as Phase 3, on metal, and a 30 minute soak with no overruns.

### Phase 5 — the demo
Dashboard: giant color-coded mode label, live "time since last command" counter, rolling
strip chart of loop period, histogram. Legible from three meters. Foxglove or a plain
web page fed from Zenoh over websocket, your call, but readable beats pretty.

Three-column jitter comparison harness: Linux `SCHED_OTHER` untuned, Linux `SCHED_FIFO 80`
+ `mlockall` + `isolcpus`, and Zephyr. Run it under `stress-ng` load on the companion.

Do not rig this. Column 2 will look respectable and should be allowed to. The claim is
"bounded, with zero tuning, no RT patch, no CPU isolation, and still a native ROS 2 node,"
not "Zephyr beats Linux." A rigged benchmark gets torn apart by exactly the people worth
convincing.

### Phase 6 — CI
Twister matrix on both targets. `docker compose` smoke test in GitHub Actions asserting the
failover timings. Once this lands, the failover policy stops being a demo and starts being
a regression test.

---

## Do not

- Use `k_sleep()` or a workqueue for control loop timing.
- Put zbus, logging, or shell anywhere on the 1 ms path. zbus is fine for telemetry fanout
  off the hot path, nowhere else.
- Let the hardware interface invent a command value on activate.
- Hardcode joint names, keys, gains, or limits outside devicetree.
- Use AS5600 or any I2C encoder as the reference target. It will not hold 1 kHz cleanly.
- Add a second joint before Phase 4 passes. It is the obvious next step and the obvious way
  to lose two weeks.
- Ship WiFi as the demo transport. Conference RF will destroy it. Ethernet only.

## Ask before assuming

- FOC/BLDC vs brushed + H-bridge. Spec assumes brushed, gimbal BLDC is a stretch goal.
- Whether the joint table should support >1 joint from day one. Spec assumes the code
  generalizes but only one joint is populated and tested.
- Fixed point vs float in the control math. Spec assumes float internally (F767ZI has an
  FPU), milli-units on the wire.
- Whether to target `rmw_zenoh` wire compatibility later. Spec assumes not, for now.
