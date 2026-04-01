# SecureVault Node — Presenter Demo Script
## Zephyr 4.4 Feature Showcase | ~18 minutes

---

## Setup Checklist (before going on stage)

- [ ] Terminal split: left = editor open at `src/auth.c:60` (Before/After comment), right = build terminal
- [ ] `west build -b native_sim samples/demo/zephyr44_showcase -- -DCONFIG_WIREGUARD=n` already compiled
- [ ] Second terminal tab pre-built with benchmark: `west build -b native_sim samples/demo/zephyr44_showcase/tests/benchmark`
- [ ] Release notes page open in browser: https://docs.zephyrproject.org/latest/releases/release-notes-4.4.html

---

## [00:00–02:00] Hook — The Problem

> "You're shipping 500 monitoring nodes into data centres across Europe. Each node sits physically
> inside a rack, unattended. You need it to:
> — only allow authorised engineers to reconfigure it locally,
> — report encrypted telemetry back to your ops platform,
> — stay within a 200 ms authentication latency SLA,
> — and conserve power when idle.
>
> In the past you'd stitch together four separate open-source libraries, write glue code,
> and hope they all compile on the same toolchain. In Zephyr 4.4 every one of those
> capabilities ships in the kernel. Let me show you."

**Point at the release notes page.** Highlight the four sections you're about to demo.

---

## [02:00–04:00] Build + Dashboard

```bash
# Already built — just show the output
west build -b native_sim samples/demo/zephyr44_showcase -- -DCONFIG_WIREGUARD=n
```

**Point at the size output at the end of the build.**

> "121 new boards in 4.4, plus Zephyr SDK 1.0 with unified GNU toolchain and experimental
> Clang/LLVM support. But here's what I love about this build — look at the footprint.
> The entire application — biometric auth, sensor polling, VPN stub, shell — fits in
> less than 256 KB of flash. That's a $3 MCU."

*If the dashboard plugin is installed:*

```bash
west build -t dashboard
```

> "The build dashboard is a 4.4 enhancement — it consolidates ROM/RAM per subsystem,
> DeviceTree details, and init-level analysis in one view. Useful for arguing with
> your hardware team about which MCU to buy."

---

## [04:00–06:00] Boot Sequence

```bash
west build -t run
```

Watch the log output. Point out each init line:

```
[INF] auth: Fingerprint sensor ready — max 100 templates, 2 samples/enroll
[INF] report: WireGuard not enabled — reporting disabled.
[INF] monitor: Monitor started — CPU freq pressure policy will track prio 1..10
[INF] main: No users enrolled — auto-enrolling demo user 1
[INF] biometrics_emul: Enrollment started for ID 1
[INF] biometrics_emul: Captured sample 1/2
[INF] biometrics_emul: Captured sample 2/2
[INF] auth: Enrollment complete for user 1
[INF] main: Shell ready.
```

> "Three things happening here: the biometric sensor initialising, the CPU frequency
> policy starting, and an automatic first enrolment so the demo is usable immediately.
> The shell is now live."

---

## [06:00–09:00] Feature ①+⑤ — Biometrics API + shell_readline()

In the running terminal:

```
uart:~$ securevault status
```

```
SecureVault Node — status
  Sensor  : #12 | temp=22.4°C | hum=46.2%
  Auth DB : 1 user(s) enrolled
  VPN     : disabled (build with CONFIG_WIREGUARD=y)
```

```
uart:~$ securevault enroll 2
```

```
Enrolling fingerprint for user 2
Place finger when ready and confirm [y/N]: _
```

> "Stop right here. This prompt is powered by `shell_readline()` — new in Zephyr 4.4.
> Before this, there was no clean way to do blocking mid-command interactive input.
> You'd have to split this into two separate commands or implement a state machine
> across callbacks. Now it's one line of code."

Type `y`, watch the capture logs:
```
Place finger… (1/2)
  Sample 1/2 — quality 60%
Lift finger and place again…
Place finger… (2/2)
  Sample 2/2 — quality 60%
Enrollment complete for user 2
```

```
uart:~$ securevault identify
```
```
Identified: user 2 — Access GRANTED
```

---

## [09:00–12:00] Feature ② — Scope-based Cleanup (Code Walkthrough)

**Switch to editor.** Open `src/auth.c`, scroll to the `auth_enroll()` function.

> "This is the code that ran when we enrolled user 2. Let me show you what's
> interesting about how it's written."

**Point at the top of the function:**

```c
scope_guard(k_mutex)(&sensor_mutex);
```

> "This single line acquires `sensor_mutex` AND registers a release that will fire
> when the function returns — on any path. No matching `k_mutex_unlock` at the bottom.
> The compiler enforces this via the `__cleanup__` attribute."

**Scroll to the defer:**

```c
scope_defer(biometric_enroll_abort)(fp);
```

> "And this one: if `auth_enroll` returns early for any reason — timeout, capture
> failure, whatever — `biometric_enroll_abort()` is called automatically to reset
> the sensor state. We don't have to remember to do that on 5 different error paths."

**Show the "Before / After" comment block at line ~65.**

> "I left the old pattern in a comment for comparison. The 'before' version has a
> `goto abort` and a `goto done` and a manual unlock at the bottom. Every experienced
> embedded developer has shipped a bug where one of those paths was missing.
> Scope cleanup makes it structurally impossible."

> "This is not a new idea — C++ has RAII, Rust has Drop, Go has defer. Zephyr 4.4
> brings it to embedded C. It works on GCC and Clang. Zero runtime overhead."

---

## [12:00–14:00] Feature ④ — CPU Frequency Pressure Policy

```
uart:~$ securevault alarm
```

```
Simulating high-temperature alarm…
(Watch the log: alarm_thread wakes → CPU freq scales UP)
[WRN] alarm_thread: ALARM: sensor threshold breached — CPU freq scaled UP
```

> "There are three threads in the background: an alarm thread at priority 1, a sensor
> sampling thread at priority 5, and an aggregation thread at priority 10.
>
> Normally only the medium and low priority threads run — that's low scheduler pressure,
> so the CPU runs at minimum P-state. When we trigger an alarm, the priority-1 thread
> becomes runnable — high pressure — and the CPU frequency policy scales up automatically.
> The application never calls `cpu_freq_pstate_set()` directly. The policy just looks
> at the scheduler state."

> "On native_sim with `CONFIG_CPU_FREQ_PSTATE_SET_STUB=y` the actual clock doesn't
> change, but all the policy logic runs. On real hardware — nRF54L15, STM32H5 —
> you'd see actual DVFS happening."

---

## [14:00–16:00] Feature ③ — WireGuard VPN (Architecture Slide)

> "Let me rebuild with WireGuard enabled."

```bash
west build -b native_sim samples/demo/zephyr44_showcase
west build -t run
```

In the shell:
```
uart:~$ net iface
```

Point at the VPN interface in the output:
```
Interface wg0 (0x...) [up]
  Type   : VIRTUAL (VPN)
  IPv4   : 198.51.100.1
```

> "There's a virtual network interface on this MCU backed by a full WireGuard
> implementation — Curve25519 key exchange, ChaCha20-Poly1305 encryption, built
> into Zephyr. The application sends a plain UDP socket to 198.51.100.2 and the
> kernel handles the crypto transparently."

**Switch to editor, open `src/report.c`, scroll to `report_send()`.**

> "Two things to notice: first, the application code is 100% standard POSIX socket
> API — `zsock_socket`, `zsock_sendto`. No WireGuard-specific calls. Second, look
> at the `scope_defer(zsock_close)(sock)` on line ~90. Same cleanup pattern from
> auth.c, now applied to a socket. The socket is guaranteed to be closed when
> `report_send` returns, whether we hit the sendto error or the happy path."

---

## [16:00–18:00] Feature ⑥ — ztest Benchmark Framework

**Switch to the benchmark terminal tab.**

```bash
west build -t run  # (pre-built benchmark)
```

Output:
```
Running test suite: biometric
─────────────────────────────────────────────────────────────
Benchmark         │   N │     mean │   stddev │      min │      max
──────────────────┼─────┼──────────┼──────────┼──────────┼─────────
match_verify      │ 100 │  152 µs  │   3.1 µs │  148 µs  │  163 µs
match_identify    │ 100 │  154 µs  │   2.9 µs │  149 µs  │  161 µs
get_capabilities  │ 500 │    0.3 µs│   0.1 µs │    0.3 µs│    0.5 µs
attr_get          │ 500 │    0.5 µs│   0.1 µs │    0.4 µs│    0.7 µs
─────────────────────────────────────────────────────────────
```

> "Our spec says authentication must complete in under 200 ms. The benchmark shows
> 152 µs — we have 1000× headroom. That's not a number I estimated; it's a measured
> statistical fact with 100 samples, using Welford's online variance algorithm so it
> doesn't need to store the full sample array.
>
> Before 4.4 you had to write this yourself — record a cycle counter, loop N times,
> compute mean by hand. Now it's four macros: ZTEST_BENCHMARK_SUITE, ZTEST_BENCHMARK.
> The framework handles everything including CSV output for CI dashboards."

---

## [18:00] Wrap-up

> "Six real Zephyr 4.4 features in one application:
>
> ① Biometrics API — new driver class, fingerprint on an MCU
> ② Scope cleanup — RAII in C, eliminates a whole class of resource bugs
> ③ WireGuard — full VPN on bare metal, standard socket API
> ④ CPU freq pressure policy — scheduler-driven DVFS, zero application code
> ⑤ shell_readline() — interactive input that actually reads like code
> ⑥ ztest benchmarks — cycle-accurate statistical perf regression testing
>
> All in a binary that fits in 256 KB. Grab the code from the Zephyr repo
> at `samples/demo/zephyr44_showcase/`. The DEMO_SCRIPT.md walks through
> everything you just saw."

---

## Backup / FAQ

**Q: Does this work on hardware today?**
> Yes. Biometrics: ZFM-x0 and GT5x drivers ship in 4.4. WireGuard: marked experimental
> but functional. CPU freq: needs SoC with DVFS support (nRF54L15, STM32H5).
> Add a board overlay for the fingerprint sensor and you're done.

**Q: What's the real match latency on actual hardware?**
> Depends on the sensor. The ZFM-x0 optical sensor takes ~300 ms for image acquisition.
> The emulator models the driver dispatch overhead (~150 µs) without the sensor I/O.
> The benchmark gives you numbers for both.

**Q: Can I use scope cleanup in my existing code?**
> Yes — it's just `CONFIG_SCOPE_CLEANUP_HELPERS=y`. The helpers work anywhere the
> toolchain supports `__attribute__((cleanup))` — GCC 3.4+, Clang 3.0+.

**Q: Is WireGuard production-ready in 4.4?**
> It's marked EXPERIMENTAL (`CONFIG_EXPERIMENTAL_WARN`). The crypto is correct and
> interoperates with the upstream WireGuard reference implementation. The networking
> integration is still stabilising. Watch 4.5 for the experimental flag to drop.
