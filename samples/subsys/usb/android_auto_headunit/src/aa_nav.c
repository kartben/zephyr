/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_nav.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "src/aa.pb.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_session.h"

LOG_MODULE_REGISTER(aa_nav, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * The next turn on a small display of its own, the way a car shows it in the
 * instrument cluster rather than on the projected screen. The phone is asked
 * for the turn as an enumeration, so what arrives is the road, which way it
 * goes and how far away it is, and the three are written as text.
 */

#if DT_HAS_ALIAS(aa_nav_display)

static const struct device *const nav_dev = DEVICE_DT_GET(DT_ALIAS(aa_nav_display));
static bool ready;

/* What the last turn and distance messages said, drawn together */
static char nav_road[24];
static const char *nav_turn = "";
static char nav_distance[16];
static bool nav_active;

static const char *turn_text(int32_t event, int32_t side, int32_t exit_number)
{
	switch (event) {
	case 1:
		return "START";
	case 3:
	case 4:
	case 5:
		return (side == AA_NAV_TURN_LEFT) ? "LEFT" : "RIGHT";
	case 6:
		return "U-TURN";
	case 7:
		return "ON RAMP";
	case 8:
		return "OFF RAMP";
	case 9:
		return (side == AA_NAV_TURN_LEFT) ? "FORK L" : "FORK R";
	case 10:
		return "MERGE";
	case 11:
	case 12:
	case 13:
		return (exit_number > 0) ? "ROUNDABT" : "ROUNDABT";
	case 14:
		return "STRAIGHT";
	case 19:
		return "ARRIVE";
	default:
		return "";
	}
}

/* NavigationManeuver.type, which splits left and right into their own values */
static const char *maneuver_text(int32_t type)
{
	switch (type) {
	case 1:
		return "START";
	case 3:
	case 5:
	case 7:
	case 9:
		return "LEFT";
	case 4:
	case 6:
	case 8:
	case 10:
		return "RIGHT";
	case 11:
	case 12:
		return "U-TURN";
	case 25:
		return "FORK L";
	case 26:
		return "FORK R";
	case 27:
	case 28:
	case 29:
		return "MERGE";
	case 30:
	case 31:
		return "ROUNDABT";
	case 36:
		return "STRAIGHT";
	case 39:
	case 40:
	case 41:
	case 42:
		return "ARRIVE";
	default:
		return "";
	}
}

static void redraw(void)
{
	if (!ready) {
		return;
	}

	(void)cfb_framebuffer_clear(nav_dev, false);

	if (!nav_active) {
		(void)cfb_print(nav_dev, "no route", 0, 0);
	} else {
		/* Three rows of the small font fit the panel's forty lines */
		(void)cfb_print(nav_dev, nav_turn, 0, 0);
		(void)cfb_print(nav_dev, nav_distance, 0, 12);
		(void)cfb_print(nav_dev, nav_road, 0, 24);
	}

	(void)cfb_framebuffer_finalize(nav_dev);
}

static void show_distance(const NavigationNextTurnDistanceEvent *ev)
{
	int32_t metres = ev->has_distance_meters ? ev->distance_meters : 0;

	if (metres >= 1000) {
		(void)snprintf(nav_distance, sizeof(nav_distance), "%d.%d km", metres / 1000,
			       (metres % 1000) / 100);
	} else {
		(void)snprintf(nav_distance, sizeof(nav_distance), "%d m", metres);
	}
}

void aa_nav_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	switch (msg_id) {
	case AA_NAV_STATUS_START:
		nav_active = true;
		nav_road[0] = '\0';
		nav_turn = "";
		nav_distance[0] = '\0';
		LOG_INF("Navigation started");
		redraw();
		break;

	case AA_NAV_STATUS_STOP:
		nav_active = false;
		LOG_INF("Navigation stopped");
		redraw();
		break;

	case AA_NAV_TURN_EVENT: {
		NavigationNextTurnEvent ev = NavigationNextTurnEvent_init_zero;

		if (aa_pb_decode(body, len, NavigationNextTurnEvent_fields, &ev) != 0) {
			break;
		}
		nav_active = true;
		if (ev.has_road) {
			(void)strncpy(nav_road, ev.road, sizeof(nav_road) - 1U);
			nav_road[sizeof(nav_road) - 1U] = '\0';
		}
		nav_turn = turn_text(ev.has_event ? ev.event : 0,
				     ev.has_turn_side ? ev.turn_side : 0,
				     ev.has_turn_number ? ev.turn_number : 0);
		LOG_INF("Next turn: %s on %s", nav_turn, nav_road);
		redraw();
		break;
	}

	case AA_NAV_DISTANCE_EVENT: {
		NavigationNextTurnDistanceEvent ev = NavigationNextTurnDistanceEvent_init_zero;

		if (aa_pb_decode(body, len, NavigationNextTurnDistanceEvent_fields, &ev) != 0) {
			break;
		}
		show_distance(&ev);
		redraw();
		break;
	}

	case AA_NAV_STATE: {
		/*
		 * What a phone that has stopped sending the two messages above
		 * sends instead. Only the step being driven is of interest.
		 */
		NavigationState state = NavigationState_init_zero;

		if (aa_pb_decode(body, len, NavigationState_fields, &state) != 0) {
			break;
		}
		if (state.steps_count == 0U) {
			break;
		}
		nav_active = true;
		if (state.steps[0].has_road) {
			(void)strncpy(nav_road, state.steps[0].road, sizeof(nav_road) - 1U);
			nav_road[sizeof(nav_road) - 1U] = '\0';
		}
		if (state.steps[0].has_maneuver && state.steps[0].maneuver.has_type) {
			nav_turn = maneuver_text(state.steps[0].maneuver.type);
		}
		LOG_INF("Next turn: %s on %s", nav_turn, nav_road);
		redraw();
		break;
	}

	default:
		LOG_DBG("Unhandled navigation message 0x%04x", msg_id);
		break;
	}
}

void aa_nav_link_down(void)
{
	nav_active = false;
	redraw();
}

/*
 * The display shares its bus with nothing else, so when it does not answer it
 * is worth saying whether anything on the bus does: that separates a wire in
 * the wrong hole from a part at a different address.
 */
static void scan_bus(void)
{
	const struct device *const bus = DEVICE_DT_GET(DT_BUS(DT_ALIAS(aa_nav_display)));
	uint8_t found = 0U;
	uint8_t addr;

	if (!device_is_ready(bus)) {
		LOG_ERR("%s is not ready either", bus->name);
		return;
	}

	for (addr = 0x08U; addr < 0x78U; addr++) {
		uint8_t probe = 0U;

		if (i2c_write(bus, &probe, 0U, addr) == 0) {
			LOG_INF("%s: something answers at 0x%02x", bus->name, addr);
			found++;
		}
	}

	if (found != 0U) {
		return;
	}

	LOG_ERR("%s: nothing answers", bus->name);

	/*
	 * Read the two lines back. A device that is connected and powered
	 * holds them high through its pull ups, so a line sitting low says
	 * the wire or the power is the problem rather than the protocol.
	 */
	{
		const struct gpio_dt_spec scl =
			GPIO_DT_SPEC_GET(DT_NODELABEL(nav_bus_probe), scl_gpios);
		const struct gpio_dt_spec sda =
			GPIO_DT_SPEC_GET(DT_NODELABEL(nav_bus_probe), sda_gpios);

		if (gpio_pin_configure_dt(&scl, GPIO_INPUT | GPIO_PULL_DOWN) == 0 &&
		    gpio_pin_configure_dt(&sda, GPIO_INPUT | GPIO_PULL_DOWN) == 0) {
			/*
			 * Pulled down from inside, so a line that still reads
			 * high is held there by a part that is connected and
			 * powered, and one that reads low is not.
			 */
			LOG_ERR("Against a pull down: SCL (D15) reads %d, SDA (D14) reads %d",
				gpio_pin_get_dt(&scl), gpio_pin_get_dt(&sda));
		}
	}
}

int aa_nav_init(void)
{
	int ret;

	if (!device_is_ready(nav_dev)) {
		LOG_WRN("No navigation display");
		scan_bus();
		return 0;
	}

	ret = display_set_pixel_format(nav_dev, PIXEL_FORMAT_MONO10);
	if (ret != 0) {
		LOG_WRN("Could not set the navigation display's format (%d)", ret);
	}

	ret = cfb_framebuffer_init(nav_dev);
	if (ret != 0) {
		LOG_ERR("Could not set the navigation display up (%d)", ret);
		return 0;
	}

	(void)cfb_framebuffer_set_font(nav_dev, 0);
	(void)display_blanking_off(nav_dev);

	ready = true;
	LOG_INF("Navigation display ready (%s)", nav_dev->name);
	redraw();

	return 0;
}

#else /* no display for it */

int aa_nav_init(void)
{
	return 0;
}

void aa_nav_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	ARG_UNUSED(body);
	ARG_UNUSED(len);
	LOG_DBG("No navigation display for message 0x%04x", msg_id);
}

void aa_nav_link_down(void)
{
}

#endif /* DT_HAS_ALIAS(aa_nav_display) */
