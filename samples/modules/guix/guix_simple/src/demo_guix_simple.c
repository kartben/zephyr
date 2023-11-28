/* This is a small demo of the high-performance GUIX graphics framework. */

#include <stdio.h>
#include "gx_api.h"
#include "gx_display.h"

#include "guix_simple_resources.h"
#include "guix_simple_specifications.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample, LOG_LEVEL_INF);
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

GX_WINDOW *pHelloScreen;

GX_WINDOW_ROOT *root;

/* Define prototypes.   */
VOID start_guix(VOID);

const static struct device *display_dev;

int main(int argc, char **argv)
{
	printf("GUIX demo...\n");

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found. Aborting sample.", display_dev->name);
		return 0;
	}

	start_guix();
	return (0);
}

static void zephyr_lcd_buffer_toggle(GX_CANVAS *canvas, GX_RECTANGLE *dirty)
{
	struct display_buffer_descriptor buf_desc;
	buf_desc.buf_size = canvas->gx_canvas_x_resolution * canvas->gx_canvas_y_resolution * 2;
	buf_desc.width = canvas->gx_canvas_x_resolution;
	buf_desc.pitch = canvas->gx_canvas_x_resolution;
	buf_desc.height = canvas->gx_canvas_y_resolution;

	GX_UBYTE *visible_buffer = canvas->gx_canvas_memory;
	display_write(display_dev, 0, 0, &buf_desc, visible_buffer);
}

UINT zephyr_graphics_driver_setup(GX_DISPLAY *display)
{
	_gx_display_driver_565rgb_setup(display, NULL, zephyr_lcd_buffer_toggle);

	return GX_SUCCESS;
}

VOID start_guix(VOID)
{
	/* Initialize GUIX.  */
	gx_system_initialize();

	gx_studio_display_configure(MAIN_DISPLAY, zephyr_graphics_driver_setup, LANGUAGE_ENGLISH,
				    MAIN_DISPLAY_THEME_1, &root);

	/* create the hello world screen */
	gx_studio_named_widget_create("simple_window", (GX_WIDGET *)root,
				      (GX_WIDGET **)&pHelloScreen);

	/* Show the root window to make it and patients screen visible.  */
	gx_widget_show(root);

	/* start GUIX thread */
	gx_system_start();

	display_blanking_off(display_dev);
}
