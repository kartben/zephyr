# Zephyr input driver audit — subtle correctness bugs

Comparative review of all 51 drivers under `drivers/input/`, grouped into eight
families (kbd-matrix core, vendor kbd backends, I2C touch, misc touch,
cap-sense, pointing devices, keys/analog, serial protocol decoders) so that
near-identical drivers could be diffed against each other. Every candidate was
then re-checked against the source with an explicit attempt to refute it; two
candidates were dropped that way and are listed at the end.

Findings are split into **likely bugs** (provable from the source tree alone)
and **needs datasheet verification** (the code is inconsistent or suspicious,
but the failure mode depends on hardware register semantics not derivable from
the tree).

---

## A. Likely bugs

### A1. `input_tma525b.c` — inverted retry check makes init fail on healthy hardware

**`tma525b_chip_init()`, drivers/input/input_tma525b.c:274**

`retry` is incremented only on a failed poll, and `break` happens *before* the
increment. So `retry == 0` is exactly the "succeeded on the first read" case,
while loop exhaustion leaves `retry == CONFIG_INPUT_TMA525B_RETRY_TIMES`.

```c
		if (ret == 0) {
			if ((read_buf[0] == 0x02U && read_buf[1] == 0x00U) ||
			    read_buf[1] == 0xFFU) {
				LOG_INF("TMA525B entered application mode");
				break;          /* retry still 0 */
			}
		}
		k_sleep(K_MSEC(TMA525B_BOOT_DELAY_MS));
		retry++;
	}

	if (retry == 0U) {              /* == "we succeeded immediately" */
		LOG_ERR("TMA525B failed to enter application mode");
		return -ENODEV;
	}
```

**Failure:** a working panel that is ready on the first read after the 120 ms
boot delay makes `tma525b_chip_init()` return `-ENODEV`, so the device is never
ready and the touchscreen is dead on every boot. The mirror case is just as
bad: an absent/broken chip exhausts the loop and returns success, after which
`tma525b_process()` fails forever with no diagnostic. A single call emits both
`LOG_INF("...entered application mode")` and
`LOG_ERR("...failed to enter application mode")`, which is the tell. Repeats on
every `PM_DEVICE_ACTION_RESUME` (line 397).

**Fix:** track success explicitly.

```diff
+	bool ready = false;
 	while (retry < CONFIG_INPUT_TMA525B_RETRY_TIMES) {
 			...
 				LOG_INF("TMA525B entered application mode");
+				ready = true;
 				break;
 	}
-	if (retry == 0U) {
+	if (!ready) {
```

**Confidence: high**

---

### A2. `input_renesas_ra_ctsu.c` — buttons never report a release

**`ctsu_renesas_ra_button_cb()` :332, `renesas_ra_ctsu_group_buttons_read()` :163**

The callback hardcodes the press value and ignores its payload:

```c
static void ctsu_renesas_ra_button_cb(const struct device *dev, void *data)
{
	ARG_UNUSED(data);
	const struct ctsu_device_cfg *cfg = dev->config;

	input_report_key(dev, cfg->event_code, 1, false, K_NO_WAIT);
}
```

and the caller walks only the *set* bits of `*data->p_button_status`, with no
previous/current diff and `NULL` as the payload. There is no button state
anywhere in `struct renesas_ra_ctsu_group_data`, and these are the only
`input_report_*` call sites for buttons in the file.

**Failure:** on rssk_ra2l1 with the rtk0eg0019b01002bj shield (in-tree, default
config), holding a pad emits `value=1` every
`CONFIG_INPUT_RENESAS_RA_CTSU_POLLING_INTERVAL_MS` (100 ms), and releasing it
emits nothing at all. Every consumer — `input_longpress`, HID keyboard, LVGL
indev — latches the key down permanently. The shield README shows `value=0`
events that the current code cannot produce.

**Fix:** add `uint64_t prev_button_status` to the group data, iterate
`changed = curr ^ prev`, pass `bool pressed = curr & BIT64(num)` to the
callback, and store `prev = curr`. The callback becomes
`input_report_key(dev, cfg->event_code, *(bool *)data, true, K_NO_WAIT)`. Note
that the `if (*data->p_button_status != 0)` guard must go — it would
short-circuit exactly the all-released transition. The RX sibling
(`input_renesas_rx_ctsu.c` `process_data()`) already does the diff correctly and
is the right in-tree reference.

**Confidence: high**

---

### A3. `input_ch9350l.c` — 16-bit relative delta sign conversion is off by one

**`CH9350L_RAWMOUSE_TO_REL`, :47, used by `ch9350l_mouse()`**

```c
#define CH9350L_FRAME_MOUSE_RELMID	0x7FFF
#define CH9350L_FRAME_MOUSE_RELNEG	0x8000

#define CH9350L_RAWMOUSE_TO_REL(_val) (_val > CH9350L_FRAME_MOUSE_RELMID ?	\
	-(CH9350L_FRAME_MOUSE_RELMID - (const int16_t)(_val - CH9350L_FRAME_MOUSE_RELNEG)) \
	: (const int16_t)_val)
```

The negative branch reduces to `raw - 65535`, not `raw - 65536`:
`0xFFFF → 0`, `0xFFFE → -1`, `0x8000 → -32767`.

This does not need a datasheet to call wrong: `0x0000` and `0xFFFF` both map to
`0`, so the mapping is not injective, and no signed 16-bit wire format has that
property. It is not sign-magnitude either (that would give `0x8001 → -1`; the
macro yields `-32766`).

**Failure:** slow leftward or upward motion — the single-count deltas the sensor
emits most often — reports `INPUT_REL_X = 0`, so the pointer does not move at
all, while slow right/down motion works normally. Larger negative deltas are
each one count short, so the cursor systematically drifts right and down.

**Fix:** drop the macro and decode directly.

```c
const int16_t x = (int16_t)sys_get_le16(&values[CH9350L_FRAME_MOUSE_X_BYTE]);
```

**Confidence: high**

---

### A4. `input_ch9350l.c` — mouse buttons reported as `INPUT_EV_DEVICE`, not `INPUT_EV_KEY`

**`ch9350l_mouse()`, :178 and :184**

Both the press and the release path use
`input_report(dev, INPUT_EV_DEVICE, ch9350l_mouse_map(...), 1/0, ...)`.
`INPUT_EV_DEVICE` is `0xef`, but the codes the binding prescribes
(`mouse-codemap = <1 INPUT_BTN_LEFT ...>`, i.e. `0x110+`) live in the `EV_KEY`
space. `ch9350l_kb()` in the same file (:124, :139) correctly uses
`INPUT_EV_KEY` under identical conditions, which rules out a deliberate vendor
namespace — this is a copy/paste slip.

**Failure:** motion works, clicks are silently dropped by every type-filtering
consumer: `subsys/input/input_longpress.c:56`,
`subsys/input/input_double_tap.c:48`, and any `INPUT_CALLBACK_DEFINE` using the
documented `evt->type == INPUT_EV_KEY` idiom. LVGL's pointer indev happens not
to be affected because it switches on `evt->code` alone.

**Fix:** `input_report_key(dev, ch9350l_mouse_map(dev, BIT(i)), 1/0, true, K_FOREVER);`
in both branches. Separately, `INPUT_REL_X` at :173 should use `sync=false` so
that X, Y and the buttons form one sync group.

**Confidence: high**

---

### A5. `input_xpt2046.c` — one transient SPI error kills the touchscreen permanently

**`xpt2046_work_handler()`, :145**

`xpt2046_isr_handler()` unconditionally removes the GPIO callback before
submitting work (:85), and the `gpio_add_callback()` at :188 is the *only* place
it is ever re-registered. The averaging loop returns early on any read failure,
jumping over it:

```c
	for (int i = 0; i < rounds; i++) {
		if (xpt2046_read_and_cumulate(&config->bus, &tx_bufs, &rx_bufs, &meas) != 0) {
			return;                 /* callback never re-added */
		}
	}
```

`gpio_manage_callback()` really unlinks the node, so the pin interrupt stays
armed but dispatches to an empty list. Nothing else — no retry, timer, or PM
hook — re-registers it.

**Failure:** a single `-EIO`/`-EBUSY` from a shared SPI bus while the user is
touching the panel, and the touchscreen reports nothing until reboot. If
`pressed` was set, the 100 ms `dwork` emits one final `BTN_TOUCH 0` and then
silence. A second identical dead end exists at :189-192 if `gpio_add_callback()`
itself fails.

**Fix:** `goto reenable;` instead of `return`, with the label just above the
`gpio_add_callback()` call. Cleaner alternative: keep the callback registered
permanently and mask/unmask with `gpio_pin_interrupt_configure_dt()` instead,
which is what most other drivers do.

**Confidence: high**

---

### A6. `input_cf1133.c` — interrupt armed before init, and left armed when init fails

**`cf1133_init()`, :298-316**

The GPIO callback is registered and the interrupt configured (:298, :304) — or
the 10 ms polling timer started (:312) — *before* `cf1133_ts_init(dev)` at :316,
which is the only assigner of `data->pixel_length`. When it fails, the function
returns at :319 with the callback still registered and the timer still running.

`cf1133_process()` declares `uint8_t buffer[1 + SUPPORTED_POINT * PIXEL_DATA_LENGTH_A]`
and sizes its I2C read as `1 + SUPPORTED_POINT * data->pixel_length` — **1 byte**
when `pixel_length` is 0 — then unconditionally dereferences `buffer[1..3]` for
the valid bit and both coordinates.

**Failure:** the error path is the stronger one. A transient boot-time NAK makes
`ts_init` fail, the device is marked not-ready — but every INT edge (or every
10 ms timer tick in polling mode) keeps calling `cf1133_process()` with
`pixel_length == 0` for the life of the system. Whenever bit 7 of the
uninitialized stack happens to be set, it emits `BTN_TOUCH=1` plus `ABS_X`/`ABS_Y`
at a garbage coordinate. The startup race (syswq at coop priority preempting the
preemptible main thread during `POST_KERNEL` init) is real too, but
board-dependent.

**Fix:** move `cf1133_ts_init(dev)` up, immediately after `k_work_init()` and
before the `#ifdef CONFIG_INPUT_CF1133_INTERRUPT` block — it only needs the I2C
bus, already validated at :273. Then nothing that can fail follows the arming
step. Add `if (data->pixel_length == 0) return -EIO;` to `cf1133_process()` as
defence in depth. The sibling `input_gt911.c` arms at the very end of init
(:434, :440) and is the right shape.

**Confidence: high**

---

### A7. `input_gt911.c` — previous-touch state is a function-scope `static`, shared by all instances

**`gt911_process()`, :127 and :129**

```c
	static uint8_t prev_points;
	struct gt911_point_reg point_reg[CONFIG_INPUT_GT911_MAX_TOUCH_POINTS];
	static struct gt911_point_reg prev_point_reg[CONFIG_INPUT_GT911_MAX_TOUCH_POINTS];
```

`struct gt911_data` holds no touch state at all, and `GT911_INIT` is expanded by
`DT_INST_FOREACH_STATUS_OKAY`, so *N* panels share one snapshot.
`gt911_process()` receives `dev` but never keys the cache off it.

**Failure:** with two `goodix,gt911` nodes, the release loop (:184-202) compares
device A's current point ids against device B's cached points, and the memcpy at
:204-205 overwrites the snapshot with whichever ran last. A release on A carries
B's coordinates, and B's still-active touch is erased from the cache, so B's real
lift finds `prev_points == 0` and emits no release — a latched `BTN_TOUCH=1`.
This is aliasing rather than a data race, so syswq serialization does not help.
No in-tree DTS declares two GT911 nodes today, which is why it has not been hit.

**Fix:** move `prev_points` and `prev_point_reg[]` into `struct gt911_data`
(`point_reg[]` stays automatic) and update the uses at :184, :187, :194, :197,
:198, :204, :205. In-tree reference: `input_tma525b.c` keeps `data->prev_touches[]`
per instance.

**Confidence: high**

---

### A8. `input_chsc5x.c` — instance macro passes the undefined token `inst`

**`CHSC5X_DEFINE(index)`, :278 and :286**

The macro parameter is `index`, but both PM macros are handed the bare token
`inst`, which is defined nowhere:

```c
#define CHSC5X_DEFINE(index)                                            \
	PM_DEVICE_DT_INST_DEFINE(inst, chsc5x_pm_action);               \
	...
	DEVICE_DT_INST_DEFINE(index, chsc5x_init, PM_DEVICE_DT_INST_GET(inst), \
```

This does *not* fail to build for a single instance — `DT_CAT` pastes it into
the syntactically valid identifier
`__pm_device_dts_ord_DT_N_INST_inst_chipsemi_chsc5x_ORD`, and the define and the
get produce the same symbol, so it compiles and links.

**Failure:** `Z_PM_DEVICE_FLAGS()` gates on `DT_NODE_EXISTS(node_id)`, which is 0
for the bogus token, so the flags are hard-coded to 0. A `wakeup-source;` or
`zephyr,pm-device-runtime-auto;` property on a chsc5x node is silently dropped
(`pm_device_wakeup_enable()` bails out, runtime-PM auto-enable never fires), and
with `CONFIG_PM_DEVICE_POWER_DOMAIN` a `power-domains` phandle is ignored
(`.domain = NULL`). Separately, the generated symbol is index-independent, so
two or more chsc5x nodes fail to build with a duplicate definition.

**Fix:** `PM_DEVICE_DT_INST_DEFINE(index, chsc5x_pm_action);` and
`PM_DEVICE_DT_INST_GET(index)`.

**Confidence: high**

---

### A9. `input_gpio_qdec.c` — encoder can get stuck in high-rate polling forever

**`gpio_qdec_sample_timer_timeout()` :176 / `gpio_qdec_poll_mode()` :94**

Line 212, `k_work_reschedule(&data->idle_work, K_MSEC(cfg->idle_timeout_ms))`,
is the *only* place `idle_work` is ever scheduled — and it sits below the early
return:

```c
	if (data->prev_step == step) {
		return;                 /* idle_work never (re)armed */
	}
```

`gpio_qdec_poll_mode()` disables the GPIO interrupts, starts the periodic sample
timer and sets `polling = 1`, but never arms `idle_work` either.

**Failure:** with the default interrupt-driven configuration (`idle-poll-time-us`
unset; the binding's own example uses `sample-time-us = 2000`,
`idle-timeout-ms = 200`), a contact bounce or EMI pulse shorter than one sample
tick triggers `gpio_qdec_cb_a` → poll mode. The first sample reads an unchanged
step and returns at :177, and nothing ever schedules the transition back. The
result is a permanent ~500 Hz CPU wakeup with the GPIO wakeup interrupts
disabled, blocking deep idle until the encoder is next physically turned. No
input is lost, but on a battery device this is a silent power regression.

**Fix:** arm the idle timeout when entering poll mode, at the end of
`gpio_qdec_poll_mode()`:

```c
k_work_reschedule(&data->idle_work, K_MSEC(cfg->idle_timeout_ms));
```

(`k_work_reschedule` is `@isr_ok`, and this path runs from the GPIO callback.)
Do *not* instead hoist the reschedule above the early return in the timer
handler — that re-arms a 200 ms timeout every 2 ms and would break the idle
transition in all configurations.

**Confidence: high**

---

### A10. `input_tsc_keys.c` — required DT property `sticky-key-timeout-ms` is parsed by nobody

**`input_tsc_callback_handler()`, :382**

`TSC_KEYS_INIT` (:429) reads only `sampling-interval-ms`, `zephyr,code`,
`noise-threshold` and the parent `group`. `struct input_tsc_keys_config` has no
sticky field, and `sticky` appears exactly once in the whole tree outside board
DTS — in `dts/bindings/input/tsc-keys.yaml:42`, where it is `required: true` and
documented as "a release event will be generated after 10 seconds".

`expect_release` has exactly two writers (:382 true, :385 false), both inside the
single `sys_ringq_full()` block, and the only release report is at :386 behind
`slope > noise_threshold`.

**Failure:** on stm32f072b_disco / stm32u083c_dk (all four in-tree nodes set
`<10000>`), the slope is a fixed 10-sample difference (~100 ms at
`sampling-interval-ms = 10`). A slow lift, a hover-off, or baseline drift below
the 50-count threshold never trips the rise test, so the key stays reported as
pressed for an unbounded time — and while latched the pad also swallows the next
press, since :381 requires `!expect_release`. A later fast lift does clear the
latch, producing an orphaned release event.

**Fix:** add `uint32_t sticky_key_timeout_ms` to the config (from
`DT_PROP(inst, sticky_key_timeout_ms)`) and a `struct k_timer` to the data.
Start/restart it at :382, stop it at :385, and on expiry emit
`input_report_key(dev, config->zephyr_code, 0, false, K_NO_WAIT)` and clear
`expect_release`. Treat 0 as disabled. Note that `oversampling` is a second
ignored required property — the window is fixed by
`CONFIG_INPUT_STM32_TSC_KEYS_BUFFER_WORD_SIZE`; either wire it up per instance
or deprecate it in the binding.

**Confidence: high**

---

### A11. `input_sbus.c` — `REPORT_FILTER` comparison wraps through unsigned

**`input_sbus_report()`, :95**

```c
	if (value >= (data->last_reported_value[channel] + REPORT_FILTER) ||
	    value <= (data->last_reported_value[channel] - REPORT_FILTER)) {
```

`value` is `unsigned int`; `last_reported_value[]` is `uint16_t`, promoting to
`int`. When `last_reported_value[channel] < REPORT_FILTER` the second operand is
negative and converts to a huge unsigned value, making the comparison
unconditionally true. `CONFIG_INPUT_SBUS_REPORT_FILTER` defaults to **1**, so
any channel currently resting at 0 hits this in the default configuration.

**Failure:** a receiver in failsafe / no-link emits structurally valid frames with
zeroed channel data (the driver checks the FRAME_LOST/FAILSAFE bits at :161-169
but only logs them, never gates reporting), so every mapped channel re-emits an
identical value-0 event every ~7-14 ms. More generally, any
`CONFIG_INPUT_SBUS_REPORT_FILTER > 1` — which the Kconfig help explicitly
encourages — disables filtering entirely for every channel resting below that
threshold. The accurate characterization is: *the filter is silently a no-op
below its own threshold*.

**Fix:** do the comparison in signed arithmetic (`value` is masked to
`SBUS_SERVO_CH_MASK`, so the cast is safe):

```c
int last = data->last_reported_value[channel];

if ((int)value >= last + REPORT_FILTER || (int)value <= last - REPORT_FILTER) {
```

Preferably mirror `input_crsf.c:188-213`, which uses an absolute-difference
helper plus an equality short-circuit.

**Confidence: high**

---

### A12. `input_renesas_rx_ctsu.c` — 64-bit button bitmap truncated into a signed `int`

**`process_data()`, :169**

```c
	int changed_buttons = data->curr_buttons_data ^ data->prev_buttons_data;
	int button_position = 0;

	while (changed_buttons != 0) {
		if (changed_buttons & BIT(0)) {
			int index = config->button_position_index[button_position];
			...
		}
		button_position++;
		changed_buttons = changed_buttons >> 1;
	}
```

`curr_buttons_data`/`prev_buttons_data` are `uint64_t` (:106-107). If bit 31 of
the XOR is set, `changed_buttons` is negative, `>>` sign-fills, the value
saturates at `-1`, and the loop never terminates. `button_position` then walks
off the end of `config->button_position_index[]` (sized to the button count,
:555) and the garbage index off `config->buttons[]` (:537), both unguarded. Bits
32-63 are discarded outright. `.num_buttons` is an unbounded DT child count
(:478) and the binding caps nothing. The RA sibling handles the same FSP status
word at full `uint64_t` width.

**Failure:** on a node with 32 or more `component-type = "button"` children
(rsk_rx130.dts already sets `max-num-sensors = <36>`, and CTSU addresses 40
channels), touching button 31 spins the system workqueue at full CPU —
`input_report()` downgrades `K_FOREVER` to `K_NO_WAIT` for syswq callers, so it
spins rather than blocks — reporting random key codes from out-of-bounds reads
until the watchdog fires.

**Fix:** keep the bitmap 64-bit and bound the loop:

```c
const int num_buttons = data->touch_instance.p_cfg->num_buttons;
uint64_t changed_buttons = data->curr_buttons_data ^ data->prev_buttons_data;

for (int button_position = 0; button_position < num_buttons && changed_buttons != 0;
     button_position++, changed_buttons >>= 1) {
	if (changed_buttons & BIT64(0)) {
		int index = config->button_position_index[button_position];

		input_report_key(dev, config->buttons[index].event,
				 (data->curr_buttons_data & BIT64(button_position)) != 0,
				 true, K_FOREVER);
	}
}
```

`BIT64()` is required — `BIT()` is `1UL << n`, undefined for n ≥ 32 on 32-bit RX.
Note the existing code also passes the raw masked bit as the event value rather
than 0/1.

**Confidence: high**

---

### A13. `input_nunchuk.c` — read errors ignored, uninitialized/stale stack reported as input

**`nunchuk_poll()`, :70**

```c
	nunchuk_read_registers(dev, buffer);      /* return value discarded */

	joystick_x = buffer[0];
	joystick_y = buffer[1];
```

`buffer[0..5]` is then unconditionally decoded into `INPUT_ABS_X`/`Y`, three
acceleration events and `BTN_C`/`BTN_Z`. `nunchuk_read_registers()` returns early
on `i2c_write_dt()` failure leaving `buffer` wholly untouched, or on
`i2c_read_dt()` failure leaving it partially written. `nunchuk_init()` in the
same file checks both of its calls (:174-184), so this is an omission rather
than a convention.

**Failure:** unplug the controller or glitch the bus mid-session. The most common
outcome is that the syswq stack slot still holds the previous sample, so the
driver silently reports **stale** input with nothing logged. Genuinely
indeterminate cases: the first poll after a runtime failure, another syswq
handler having clobbered the region between polls, and a partial `i2c_read_dt()`
where a half-updated `buffer[5]` flips `BTN_C`/`BTN_Z` — a phantom press with no
matching release.

**Fix:** capture the return value and skip decoding on error, jumping to the
reschedule at the end of the function. `input_adafruit_seesaw_gamepad.c` and
`input_modulino_buttons.c` both already do this.

**Confidence: high**

---

### A14. `input_xec_kbd.c` — PM policy lock leaked on every suspend

**`xec_kbd_set_detect_mode()`, :96**

```c
	if (enabled) {
		if (data->pm_lock_taken) {
			pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
		}
		...
	} else {
		pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
		data->pm_lock_taken = true;
	}
```

`pm_lock_taken` is set true and **never cleared**, so it only ever suppresses the
first `_put`. `pm_policy_state_lock_get()` is refcounted. Compounding it, in
`input_kbd_matrix_polling_thread()` (`input_kbd_matrix.c:346-356`) the
`set_detect_mode(dev, true)` — the `_put` side — sits inside
`if (!input_kbd_matrix_is_suspended(dev))`, while `set_detect_mode(dev, false)`
after `k_sem_take()` is unconditional. `input_kbd_matrix_pm_action()` gives the
semaphore on both SUSPEND and RESUME. The MEC dtsi files really do define a
`suspend-to-idle` state, so this is not a no-op.

**Failure:** a net +1 refcount per suspend/resume cycle, unbounded, and the lock
is *held* while the device is suspended, which is backwards from the intent.
With `CONFIG_PM_DEVICE_RUNTIME=y`, `input_kbd_matrix_common_init()` calls
`pm_device_runtime_enable()`, which issues a SUSPEND at boot — so the polling
thread parks holding one reference forever and the EC can never enter its
low-power idle state.

**Fix:** two parts; idempotency alone is not enough.
1. Guard both directions in the driver:
   `if (data->pm_lock_taken) { put; data->pm_lock_taken = false; }` and
   `if (!data->pm_lock_taken) { get; data->pm_lock_taken = true; }`. This caps
   the leak at 1.
2. Make the release actually happen: in `input_kbd_matrix.c:346-356`, move
   `api->set_detect_mode(dev, true)` out of the
   `if (!input_kbd_matrix_is_suspended(dev))` block, keeping only the column
   drive and the `read_row` re-check gated, so the "leaving polling"
   notification is delivered every iteration. Backends that only toggle
   interrupt-enable bits are unaffected by that change.

**Confidence: high**

---

### A15. `input_analog_axis_settings.c` — stack buffer overflow in calibration save

**`analog_axis_calibration_save()`, :101**

```c
	struct analog_axis_calibration cal[MAX_AXES];   /* CONFIG_..._MAX_AXES, default 8 */
	...
	axes = analog_axis_num_axes(dev);               /* DT child count, unbounded */
	for (int i = 0; i < axes; i++) {
		analog_axis_calibration_get(dev, i, &cal[i]);
	}
	ret = settings_save_one(path, &cal[0], sizeof(struct analog_axis_calibration) * axes);
```

There is no `BUILD_ASSERT` and no runtime check anywhere;
`analog_axis_validate()` bounds only the low end. The load path is accidentally
safe (it caps `read_cb` at `sizeof(cal)` and length-checks before the loop).

**Failure:** a board with 9 or more axes that leaves the Kconfig at its default
of 8 gets a 6-byte out-of-bounds stack write per excess axis, after which
`settings_save_one()` reads out of bounds and persists adjacent stack contents to
flash. Which locals get clobbered is layout-dependent; return-address corruption
is possible. No in-tree DTS exceeds 8 axes today (fs_i6s has 6).

**Fix:** bounds-check after `axes = analog_axis_num_axes(dev);`:

```c
if (axes > MAX_AXES) {
	LOG_ERR("%s: too many axes: %d > %d, increase "
		"CONFIG_INPUT_ANALOG_AXIS_SETTINGS_MAX_AXES", dev->name, axes, MAX_AXES);
	return -ENOSPC;
}
```

A `BUILD_ASSERT` is not available here — this is a separate translation unit with
no per-instance visibility.

**Confidence: high**

---

### A16. `input_cap12xx.c` — `signal-guard` / `calib-sensitivity` indexed by channel count but defaulted to 3 entries

**`cap12xx_init()` :210, `cap12xx_set_calsens()` :111**

`.input_channels = DT_INST_PROP_LEN(index, input_codes)` (:319) bounds both
loops, but the backing arrays are sized purely from their own DT properties
(:313-316):

```c
	static const uint8_t cap12xx_signal_guard_##index[] = DT_INST_PROP(index, signal_guard);
	static const uint8_t cap12xx_calib_sensitivity_##index[] =
		DT_INST_PROP(index, calib_sensitivity);
```

`microchip,cap12xx.yaml` advertises 3-, 6- and 8-channel parts, makes both
properties optional, and gives each a fixed **3**-element default
(`default: [0, 0, 0]`). `edtlib` returns the default verbatim with no padding to
the channel count. There is no `BUILD_ASSERT` and no cross-check.

**Failure:** a CAP1206 node with 6 `input-codes` and both optional properties
omitted reads `signal_guard[3..5]` past the end of a 3-byte `.rodata` array.
Nonzero adjacent bytes set bits in `guarded_channels`, turning real sensing
channels into signal guards so those keys never report anything. Then
`calsens[3..5]` reads out of bounds too: any byte > 4 returns `-EINVAL` and init
fails permanently, while a byte of 0 passes the guard but makes `ilog2(0)` write
a bogus gain — the quieter failure. Deterministic per build, and it flips with
link order. The sole in-tree node (build_all, 3 channels) spells both arrays out,
which is why this has not been seen.

**Fix:** add `BUILD_ASSERT`s in `CAP12XX_INIT(index)` requiring both array
lengths to be at least `DT_INST_PROP_LEN(index, input_codes)` (`_LEN` is emitted
for defaulted array properties too), harden `cap12xx_set_calsens()` against a
zero entry, and document the 3-element-default caveat in the binding. Clamping
with `MIN()` is not a sufficient fix on its own — it silently leaves the extra
channels at their reset defaults.

**Confidence: high**

---

### A17. `input_bee_keyscan.c` — auto-scan mode reports input from ISR context with `K_FOREVER`

**`bee_keyscan_isr()`, :247 and :256**

Under `#if CONFIG_BEE_INPUT_KEYSCAN_AUTOSCAN_MODE` the ISR calls
`bee_keyscan_process_matrix()` directly, whereas the `#else` branch does
`k_work_submit(&data->work)` (:250). That reaches
`input_kbd_matrix_update_state()` → `input_report_abs/key(..., K_FOREVER)`
(`input_kbd_matrix.c:217-219`, hardcoded with no override).
`subsys/input/input.c:53-58` downgrades the timeout only when the caller is the
syswq thread — there is no `k_is_in_isr()` check.

**Failure:** with `CONFIG_ASSERT=y` the
`__ASSERT(!arch_is_in_isr() || K_TIMEOUT_EQ(timeout, K_NO_WAIT))` at
`kernel/msg_q.c:133` sits at the top of `put_msg_in_queue()`, before the space
check, so it trips on the *first* reported event. With `CONFIG_ASSERT=n`,
`z_pend_curr()` begins with an unconditional
`if (arch_is_in_isr()) { k_panic(); }`, so the outcome is a panic rather than a
hang. Both are fatal. `CONFIG_BEE_INPUT_KEYSCAN_AUTOSCAN_MODE` is `default n`
and no in-tree board or test enables it, so CI does not cover this — it breaks
any user who selects the documented auto-scan mode.

**Fix:** move `struct k_work work; const struct device *dev;` out of the
`#ifndef` guard (:75-78), always `k_work_init()` in `bee_keyscan_init()`, and
replace the direct calls at :247 and :256 with `k_work_submit(&data->work)`. Two
details a patch must get right: the ALL_RELEASE path passes
`new_press_num = 0, NULL` while the work handler reads `data->new_press_num` /
`data->new_keys`, so set `data->new_press_num = 0` before submitting; and
preserve the SCAN_END unmask ordering (:248 vs :219), or a second scan-end IRQ
can overwrite `data->new_keys` before the queued work runs.

The root cause is worth fixing at the subsystem level too: the hardcoded
`K_FOREVER` at `input_kbd_matrix.c:217-219` is what makes any ISR-context backend
unsafe. `input_bflb_irx.c` (:196, :207, :232, :273) has the same ISR +
`K_FOREVER` pattern, so this is a class of bug rather than a one-off.

**Confidence: high** (defect), reachability gated behind an opt-in Kconfig

---

## B. Needs datasheet verification

These are real inconsistencies in the code, but whether they produce the
described failure depends on hardware behaviour that could not be settled from
the tree.

### B1. `input_bee_keyscan.c` — auto-scan key release may never complete debouncing

**`bee_keyscan_isr()` ALL_RELEASE branch, :256**

The auto-scan ALL_RELEASE branch calls `bee_keyscan_process_matrix(dev, 0, NULL)`
exactly once, and nothing schedules a follow-up: both the idle handling and the
`k_timer_start(..., poll_period_us)` re-arm at the end of `process_matrix()` are
inside `#ifndef CONFIG_BEE_INPUT_KEYSCAN_AUTOSCAN_MODE`.
`input_kbd_matrix_update_state()` only *arms* debouncing on the call that first
observes the change (it stamps `scan_clk_cycle[]` at :142, then evaluates
`deb_t_us == 0 < debounce_up_us` at :194), so a second call at least
`debounce-up-ms` later is mandatory. The driver never calls
`input_kbd_matrix_common_init()`, so the generic polling thread does not exist —
the ISR is the only thing driving `update_state()`.

**Failure if confirmed:** worse than a missing release. Afterwards
`matrix_stable_state[c]` still has `BIT(r)` while `matrix_previous_state[c]` is 0,
so pressing the *same* key again debounces down and then hits
`if ((matrix_stable_state[c] & mask) == row_bit) continue;`
(`input_kbd_matrix.c:205`) — no press event either. The key emits `BTN_TOUCH=1`
exactly once, on the very first press, then goes silent forever. Timing is not
marginal: `debounce-up-ms` defaults to 20 against a `release-time-us` of 5000.

**Datasheet question:** does the Realtek keyscan block stop auto-scanning and
stop raising `KEYSCAN_INT_SCAN_END` once ALL_RELEASE fires? The HAL keyscan
sources are not in this tree. In-tree evidence favours "yes" — ALL_RELEASE and
the release-detect timer are enabled *only* in auto mode (:130-134), and the
binding describes `release-time-us` as "Time to detect all keys released in
auto-scan mode". If the block instead keeps raising SCAN_END on empty scans, the
debounce drains on its own and there is no bug.

**Fix:** add a `k_work_delayable` for the auto-scan build that keeps calling
`input_kbd_matrix_update_state(dev)` at `poll_period_us` while
`input_kbd_matrix_active(dev)` is true, with `matrix_new_state[]` left zeroed;
cancel it when a new SCAN_END arrives so the ISR stays authoritative. Fix
together with A17.

**Confidence: medium**

---

### B2. `input_realtek_rts5912_kbd.c` — KBM registers programmed while the module clock/power is gated

**`input_kbd_matrix_pm_action_resume()` :211-223, `..._suspend()` :168**

Resume does `pinctrl_apply_state(DEFAULT)` → `ctrl |= KSOTYPE` → `scan_out = 0` →
KSO18/19EN → `int_en |= ksi_mask` → **then** `clock_control_on()` at :223.
Suspend does `clock_control_off()` **first** at :168, then all of its register
writes. Both are the reverse of `rts5912_kbd_init()`, which enables the clock at
:121 before every register write (:127-149). The clock cell is
`PERIPH_GRP0_KBM_CLKPWR`, and `rts5912_periph_clock_control()` clears
`PERICLKPWR0 &= ~BIT(clk_idx)` — clock *and* power, by the register name.

**Failure if confirmed:** the gated block loses register state, the resume writes
are lost, and KBM comes back with `int_en = 0` and KSOTYPE clear — the keyboard
stops generating KSI interrupts after the first suspend/resume cycle. If writes
are dropped but content is retained, the two errors cancel and the keyboard
works, but suspend silently never deasserts the KSOs or masks KSI, so the
low-power/wake configuration is never applied. In no reading is the current
order correct. Reachability is good: `input_kbd_matrix_common_init()` calls
`pm_device_runtime_enable()`, which issues SUSPEND immediately on an ACTIVE
device, so with `CONFIG_PM_DEVICE_RUNTIME` the clock is gated right after init and
every resume takes the inverted path. rts5912_evb enables `&kbd` with a full
pinctrl sleep state.

**Datasheet question:** does clearing the `PERICLKPWR0` bit remove power and reset
KBM register state, or only stop the functional clock?

**Fix:** in resume, move `clock_control_on()` to immediately after the
`pinctrl_apply_state(DEFAULT)` block; in suspend, move `clock_control_off()` to
the very end, after all register writes and the `PINCTRL_STATE_SLEEP` apply,
preserving the existing error returns. If the block does lose state, resume must
additionally restore the KSI8EN/KSI9EN bits that `rts5912_kbd_init()` sets at
:129-135 and that resume currently never touches.

**Confidence: medium**

---

### B3. `input_pinnacle.c` — data-ready interrupt armed before the callback is registered, and no initial status drain

**`pinnacle_init_interrupt()`, :724**

The order is `gpio_pin_configure_dt(GPIO_INPUT)` →
`gpio_pin_interrupt_configure_dt(GPIO_INT_EDGE_TO_ACTIVE)` (:724) →
`gpio_init_callback()` (:730) → `gpio_add_callback()` (:733). The only runtime
STATUS1 clear is in `pinnacle_sample_fetch()` (:638), reachable only from the
work handler and therefore only from the GPIO callback — so a lost or
never-generated edge is unrecoverable. There is no PM action, no poll and no
re-arm anywhere in the file.

The ordering by itself is not unique — `input_pat912x.c` does the same. What
pinnacle lacks relative to pat912x is the initial drain (`k_work_submit()` at
pat912x :287).

**Failure if confirmed:** the feed is disabled after the software reset (:774), so
SW_DR cannot assert before `FEED_CONFIG1` at :854. With a finger already on the
pad, SW_DR asserts within ~10 ms; if that lands during the ~100-300 µs of
register writes between :854 and :724, HW_DR is already high when the edge
interrupt is configured, no rising edge is ever produced, and the trackpad is
silent until power cycle. Low-single-digit-percent per boot with a finger
present, plus the ~10-instruction window between :724 and :733.

**Datasheet question:** is HW_DR a sticky *level*, asserted while SW_DR/SW_CC are
set and cleared only by the host's STATUS1 write? Only
`dts/bindings/input/cirque,pinnacle-common.yaml` ("active high when SW_DR or
SW_CC are asserted") and the driver's own error string at :640 support this
in-tree.

**Fix:** move `gpio_init_callback()` + `gpio_add_callback()` above
`gpio_pin_interrupt_configure_dt()` — matching `input_paw32xx.c` (:386, :398) and
`input_pmw3610.c` (:509, :521) — and, critically, `k_work_submit(&drv_data->work)`
after arming, to drain an already-asserted level. The reorder alone does not fix
a true edge-detect controller (nRF GPIOTE IN, STM32 EXTI): if the level is
already active there is simply never an edge.

**Confidence: medium**

---

### B4. `input_pat912x.c` — RES_X/RES_Y written without disabling the chip write protect

**`pat912x_set_resolution()`, :161 and :174**

`PAT912X_WRITE_PROTECT 0x09`, `WRITE_PROTECT_ENABLE 0x00` and
`WRITE_PROTECT_DISABLE 0x5a` are defined (:33, :48, :49) and referenced exactly
once each — at their own definition. `pat912x_set_resolution()` writes registers
0x0d and 0x0e with bare `i2c_reg_write_byte_dt()`. The sibling
`input_paw32xx.c` brackets its 0x0d/0x0e/0x05/0x19 writes in `0x09 = 0x5a` /
`0x09 = 0x00`.

**Failure if confirmed:** a board setting `res-x-cpi`/`res-y-cpi`, or an
application calling the public `pat912x_set_resolution()` from
`include/zephyr/input/input_pat912x.h`, gets writes that are ACKed on the bus but
discarded by the chip. The function returns 0, nothing is logged, and the sensor
silently keeps its default CPI.

**Datasheet question:** does 0x09 reset to 0x00 (protected), and do 0x0d/0x0e fall
inside the protected range? Circumstantial support: Prusa-Firmware's `pat9125.cpp`
performs the same `0x06 = 0x97` soft reset and still writes `0x09 = 0x5a` before
touching RES_X/RES_Y (its comment says `0x09 = 0x00` "prevents writing to
registers over 0x09"), and the Linux PAT9125 driver does the same.

**Fix:** hoist the two range checks above the unlock (so an `-EINVAL` caller never
leaves the chip unprotected), write `0x09 = 0x5a`, do the two conditional RES
writes, then re-lock `0x09 = 0x00` on a common exit path, propagating the first
error.

Note: the `OPERATION_MODE` (0x05) half of this concern does **not** hold. 0x05 is
below 0x09 and outside the documented protected range, and neither reference
implementation guards it — there is no basis for saying `sleep1-enable` /
`sleep2-enable` are ineffective. `pat912x_configure()` should be left alone.

**Confidence: medium**

---

## C. Candidates examined and rejected

Recorded so they are not re-raised.

- **`input_mcux_kpp.c`** — "`read_keys_old` is seeded from the live key state
  while `key_pressed_number` stays 0, giving a permanently missed first press."
  Refuted: the seeding path cannot produce the claimed divergence, because the
  subsequent scan re-derives both from the same read.

- **`input_adafruit_seesaw_gamepad.c`** — "joystick Y axis is not inverted while
  X is, so Y reports upside-down." Refuted: the asymmetry is deliberate and
  matches the physical panel orientation documented for the board; both axes end
  up in the conventional orientation.

---

## D. Notes on method and coverage

- All 51 drivers under `drivers/input/` were read in full, in eight family
  groups, so that near-identical drivers could be diffed against one another.
  The six vendor keyboard-matrix backends (ITE ×3, NPCX, XEC, RTS5912) and the
  FocalTech-family touch drivers were compared function by function — A14 and
  the `read_row` masking note below came out of exactly that comparison.
- Every finding above was re-checked against the source with an explicit attempt
  to refute it before being reported; the two entries in section C were dropped
  at that stage.
- The findings in A1-A16 are provable from this tree alone. A17 is provable but
  sits behind an opt-in `default n` Kconfig. Section B depends on hardware
  behaviour and should not be patched without the relevant datasheet.

### Additional latent issue (low reachability, worth folding into A14)

**`input_xec_kbd.c` `xec_kbd_read_row()`, :65** applies the complement *outside*
the mask:

```c
	return ~(sys_read32(base + XEC_KBD_KSI_IN_OFS) & 0xff);
```

which yields `0xFFFFFF00 | ~ksi` on a `uint32_t`; only the narrowing to
`kbd_row_t` hides it. `CONFIG_INPUT_KBD_MATRIX_16_BIT_ROW` is a global,
user-selectable bool with no dependency excluding XEC and no `BUILD_ASSERT`
blocking the combination. XEC is the only in-tree backend that can return bits
above its row count — npcx masks with `BIT_MASK(row_size)`, rts5912 XORs with it,
it8801 does `(~value) & 0xff`, and it8xxx2/it51xxx use `uint8_t` throughout.

In a 16-bit-row build the result is `0xFF00 | (~ksi & 0xff)`. The in-tree MEC
boards use `col-size = 16` with no `actual-key-mask` and no `no-ghostkey-check`,
so `input_kbd_matrix_ghosting()` sees at least 8 common bits between any two
columns and reports ghosting on *every* scan — `update_state()` is never reached
and the keyboard reports nothing at all. `input_kbd_matrix_active()` is also
permanently true, so the driver polls forever and never releases the PM policy
lock from A14.

Fix: `return ~sys_read32(base + XEC_KBD_KSI_IN_OFS) & BIT_MASK(cfg->common.row_size);`
(minimally, `(~sys_read32(...)) & 0xff`). No in-tree configuration combines XEC
with 16-bit rows today. **Confidence: medium**
