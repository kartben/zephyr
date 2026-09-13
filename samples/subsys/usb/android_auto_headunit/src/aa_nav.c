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

#if DT_HAS_ALIAS(aa_nav_display) && DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(aa_nav_display))

static const struct device *const nav_dev = DEVICE_DT_GET(DT_ALIAS(aa_nav_display));
static bool ready;

/* What the last turn and distance messages said, drawn together */
static struct turn nav_turn;
static char nav_distance[8];
static char nav_unit[4];
static bool nav_active;

/*
 * Every turn a phone can name is drawn as one of a handful of shapes, mirrored
 * for the side it goes to. Anything unrecognised is drawn as a straight ahead,
 * which is wrong far less often than an empty panel is useless.
 */
enum arrow {
	ARROW_STRAIGHT,
	ARROW_SLIGHT,
	ARROW_NORMAL,
	ARROW_SHARP,
	ARROW_U_TURN,
	ARROW_RAMP,
	ARROW_FORK,
	ARROW_MERGE,
	ARROW_ROUNDABOUT,
	ARROW_DESTINATION,
	ARROW_DEPART,
};

struct turn {
	enum arrow arrow;
	bool left;
};

/* NavigationNextTurnEvent.event, with the side carried separately */
static struct turn turn_of_event(int32_t event, int32_t side)
{
	bool left = (side == AA_NAV_TURN_LEFT);

	switch (event) {
	case 1:
		return (struct turn){ARROW_DEPART, left};
	case 3:
		return (struct turn){ARROW_SLIGHT, left};
	case 4:
		return (struct turn){ARROW_NORMAL, left};
	case 5:
		return (struct turn){ARROW_SHARP, left};
	case 6:
		return (struct turn){ARROW_U_TURN, left};
	case 7:
	case 8:
		return (struct turn){ARROW_RAMP, left};
	case 9:
		return (struct turn){ARROW_FORK, left};
	case 10:
		return (struct turn){ARROW_MERGE, left};
	case 11:
	case 12:
	case 13:
		return (struct turn){ARROW_ROUNDABOUT, left};
	case 19:
		return (struct turn){ARROW_DESTINATION, left};
	case 2:
	case 14:
	default:
		return (struct turn){ARROW_STRAIGHT, false};
	}
}

/* NavigationManeuver.type, which folds the side into the value */
static struct turn turn_of_maneuver(int32_t type)
{
	static const struct {
		int32_t type;
		enum arrow arrow;
		bool left;
	} table[] = {
		{1, ARROW_DEPART, false},     {3, ARROW_SLIGHT, true},
		{4, ARROW_SLIGHT, false},     {5, ARROW_SLIGHT, true},
		{6, ARROW_SLIGHT, false},     {7, ARROW_NORMAL, true},
		{8, ARROW_NORMAL, false},     {9, ARROW_SHARP, true},
		{10, ARROW_SHARP, false},     {11, ARROW_U_TURN, true},
		{12, ARROW_U_TURN, false},    {13, ARROW_RAMP, true},
		{14, ARROW_RAMP, false},      {15, ARROW_RAMP, true},
		{16, ARROW_RAMP, false},      {17, ARROW_RAMP, true},
		{18, ARROW_RAMP, false},      {19, ARROW_U_TURN, true},
		{20, ARROW_U_TURN, false},    {21, ARROW_RAMP, true},
		{22, ARROW_RAMP, false},      {23, ARROW_RAMP, true},
		{24, ARROW_RAMP, false},      {25, ARROW_FORK, true},
		{26, ARROW_FORK, false},      {27, ARROW_MERGE, true},
		{28, ARROW_MERGE, false},     {29, ARROW_MERGE, false},
		{30, ARROW_ROUNDABOUT, false}, {31, ARROW_ROUNDABOUT, false},
		{32, ARROW_ROUNDABOUT, false}, {33, ARROW_ROUNDABOUT, false},
		{34, ARROW_ROUNDABOUT, true},  {35, ARROW_ROUNDABOUT, true},
		{36, ARROW_STRAIGHT, false},   {37, ARROW_STRAIGHT, false},
		{38, ARROW_STRAIGHT, false},   {39, ARROW_DESTINATION, false},
		{40, ARROW_DESTINATION, false}, {41, ARROW_DESTINATION, true},
		{42, ARROW_DESTINATION, false},
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(table); i++) {
		if (table[i].type == type) {
			return (struct turn){table[i].arrow, table[i].left};
		}
	}

	return (struct turn){ARROW_STRAIGHT, false};
}

/* The arrow occupies the left square of the panel, the distance the rest */
#define ARROW_SIZE 40
#define TEXT_X     (ARROW_SIZE + 2)

static void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
	const struct cfb_position a = {.x = x0, .y = y0};
	const struct cfb_position b = {.x = x1, .y = y1};

	(void)cfb_draw_line(nav_dev, &a, &b);
}

/* Mirrored about the arrow's middle when the turn goes the other way */
static int16_t mx(int16_t x, bool left)
{
	return left ? (int16_t)(ARROW_SIZE - 1 - x) : x;
}

static void line_m(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool left)
{
	line(mx(x0, left), y0, mx(x1, left), y1);
}

/* A head at the end of a stroke, pointing the way the stroke was going */
static void head(int16_t x, int16_t y, int16_t dx, int16_t dy, bool left)
{
	const int16_t wing = 6;

	line_m(x, y, (int16_t)(x - dx * wing - dy * wing / 2),
	       (int16_t)(y - dy * wing + dx * wing / 2), left);
	line_m(x, y, (int16_t)(x - dx * wing + dy * wing / 2),
	       (int16_t)(y - dy * wing - dx * wing / 2), left);
}

static void draw_arrow(struct turn t)
{
	const bool l = t.left;

	switch (t.arrow) {
	case ARROW_SLIGHT:
		line_m(20, 38, 20, 24, l);
		line_m(20, 24, 32, 8, l);
		head(32, 8, 1, -1, l);
		break;

	case ARROW_NORMAL:
		line_m(14, 38, 14, 14, l);
		line_m(14, 14, 32, 14, l);
		head(32, 14, 1, 0, l);
		break;

	case ARROW_SHARP:
		line_m(14, 38, 14, 20, l);
		line_m(14, 20, 32, 32, l);
		head(32, 32, 1, 1, l);
		break;

	case ARROW_U_TURN:
		line_m(28, 38, 28, 16, l);
		line_m(28, 16, 14, 16, l);
		line_m(14, 16, 14, 30, l);
		head(14, 30, 0, 1, l);
		break;

	case ARROW_RAMP:
		/* A gentler bend than a turn, as a slip road is */
		line_m(16, 38, 16, 26, l);
		line_m(16, 26, 22, 16, l);
		line_m(22, 16, 32, 10, l);
		head(32, 10, 2, -1, l);
		break;

	case ARROW_FORK:
		line_m(20, 38, 20, 24, l);
		line_m(20, 24, 8, 8, l);
		line_m(20, 24, 32, 8, l);
		head(32, 8, 1, -1, l);
		break;

	case ARROW_MERGE:
		line_m(8, 38, 20, 22, l);
		line_m(32, 38, 20, 22, l);
		line_m(20, 22, 20, 6, l);
		head(20, 6, 0, -1, l);
		break;

	case ARROW_ROUNDABOUT: {
		const struct cfb_position centre = {.x = mx(20, l), .y = 18};

		(void)cfb_draw_circle(nav_dev, &centre, 9);
		line_m(20, 38, 20, 27, l);
		line_m(29, 14, 36, 8, l);
		head(36, 8, 1, -1, l);
		break;
	}

	case ARROW_DESTINATION: {
		/* A flag on a pole rather than an arrow */
		line_m(14, 38, 14, 6, l);
		line_m(14, 6, 30, 12, l);
		line_m(30, 12, 14, 18, l);
		break;
	}

	case ARROW_DEPART:
		line_m(20, 38, 20, 12, l);
		head(20, 12, 0, -1, l);
		line(mx(12, l), 38, mx(28, l), 38);
		break;

	case ARROW_STRAIGHT:
	default:
		line(20, 38, 20, 8);
		head(20, 8, 0, -1, false);
		break;
	}
}

static void redraw(void)
{
	if (!ready) {
		return;
	}

	(void)cfb_framebuffer_clear(nav_dev, false);

	if (!nav_active) {
		(void)cfb_print(nav_dev, "--", 0, 12);
	} else {
		draw_arrow(nav_turn);
		(void)cfb_print(nav_dev, nav_distance, TEXT_X, 2);
		(void)cfb_print(nav_dev, nav_unit, TEXT_X, 20);
	}

	(void)cfb_framebuffer_finalize(nav_dev);
}

/*
 * Split so the number and its unit sit on their own rows: the panel is only
 * three of the small font's characters wide beside the arrow.
 */
static void show_distance(const NavigationNextTurnDistanceEvent *ev)
{
	int32_t metres = ev->has_distance_meters ? ev->distance_meters : 0;

	if (metres >= 10000) {
		(void)snprintf(nav_distance, sizeof(nav_distance), "%d", metres / 1000);
		(void)strcpy(nav_unit, "km");
	} else if (metres >= 1000) {
		(void)snprintf(nav_distance, sizeof(nav_distance), "%d.%d", metres / 1000,
			       (metres % 1000) / 100);
		(void)strcpy(nav_unit, "km");
	} else {
		(void)snprintf(nav_distance, sizeof(nav_distance), "%d", metres);
		(void)strcpy(nav_unit, "m");
	}
}

void aa_nav_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	switch (msg_id) {
	case AA_NAV_STATUS_START:
		nav_active = true;
		nav_turn = (struct turn){ARROW_STRAIGHT, false};
		nav_distance[0] = '\0';
		nav_unit[0] = '\0';
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
		nav_turn = turn_of_event(ev.has_event ? ev.event : 0,
					 ev.has_turn_side ? ev.turn_side : 0);
		LOG_INF("Next turn: shape %d%s on %s", nav_turn.arrow,
			nav_turn.left ? " left" : "", ev.has_road ? ev.road : "");
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
		if (state.steps[0].has_maneuver && state.steps[0].maneuver.has_type) {
			nav_turn = turn_of_maneuver(state.steps[0].maneuver.type);
		}
		LOG_INF("Next turn: shape %d%s on %s", nav_turn.arrow,
			nav_turn.left ? " left" : "",
			state.steps[0].has_road ? state.steps[0].road : "");
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

	/* The smallest font linked in: three of it fit beside the arrow */
	{
		int fonts = cfb_get_numof_fonts(nav_dev);
		uint8_t best = 0U;
		uint8_t best_w = UINT8_MAX;
		int i;

		for (i = 0; i < fonts; i++) {
			uint8_t w, h;

			if (cfb_get_font_size(nav_dev, (uint8_t)i, &w, &h) == 0 && w < best_w) {
				best_w = w;
				best = (uint8_t)i;
			}
		}
		(void)cfb_framebuffer_set_font(nav_dev, best);
	}
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

#endif /* navigation display */
