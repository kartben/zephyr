/*
 * Copyright (c) 2023 Trackunit Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>

#include <zephyr/drivers/display.h>
#include <lvgl.h>

#include <zephyr/net/socket.h>

#define TILE_SIZE 256 // Standard OSM tile size in pixels

#define MIN_ZOOM_LEVEL     0
#define MAX_ZOOM_LEVEL     19
#define DEFAULT_ZOOM_LEVEL 18
uint8_t current_zoom_level = DEFAULT_ZOOM_LEVEL;

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_PNG_DECODER_TEST
#define LV_ATTRIBUTE_IMG_PNG_DECODER_TEST
#endif

lv_obj_t *img;
lv_obj_t *btn_zoom_in, *btn_zoom_out;

static lv_img_dsc_t my_png = {
	.header.always_zero = 0,
	.header.reserved = 0,
	.header.w = TILE_SIZE,
	.header.h = TILE_SIZE,
	.header.cf = LV_IMG_CF_RAW_ALPHA,
	.data = NULL,
	.data_size = 0,
};

LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_PNG_DECODER_TEST uint8_t
	png_raw[7 * 1024];

#define SERVER_HOST "tile.openstreetmap.org"
#define SERVER_PORT 80
#define REQUEST_FMT                                                                                \
	"GET /%d/%d/%d.png HTTP/1.1\r\n"                                                           \
	"Host: " SERVER_HOST "\r\n"                                                                \
	"User-Agent: Zephyr RTOS/1.0\r\n"                                                          \
	"Connection: close\r\n\r\n"
#define SSTRLEN(s) (sizeof(s) - 1)
#define CHECK(r)                                                                                   \
	{                                                                                          \
		if (r == -1) {                                                                     \
			printf("Error: " #r "\n");                                                 \
			exit(1);                                                                   \
		}                                                                                  \
	}

// static void zoom_in_out(lv_event_t *e)
// {
// 	lv_obj_t *btn = lv_event_get_target(e);
// 	if (btn == btn_zoom_in) {
// 		if (current_zoom_level < MAX_ZOOM_LEVEL) {
// 			current_zoom_level++;
// 		}
// 	} else if (btn == btn_zoom_out) {
// 		if (current_zoom_level > MIN_ZOOM_LEVEL) {
// 			current_zoom_level--;
// 		}
// 	}
// }

// Function to calculate tile numbers based on latitude, longitude, and zoom level
void lat_lon_to_tile(double latitude, double longitude, uint8_t zoom, int *tile_x, int *tile_y)
{
	double lat_rad = latitude * M_PI / 180.0;
	int n = 1 << zoom;
	*tile_x = (int)floor((longitude + 180.0) / 360.0 * n);
	*tile_y = (int)floor((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n);
}

static char buf[256];
static char full_response[7 * 1024];

void dump_addrinfo(const struct addrinfo *ai)
{
	printf("addrinfo @%p: ai_family=%d, ai_socktype=%d, ai_protocol=%d, "
	       "sa_family=%d, sin_port=%x\n",
	       ai, ai->ai_family, ai->ai_socktype, ai->ai_protocol, ai->ai_addr->sa_family,
	       ((struct sockaddr_in *)ai->ai_addr)->sin_port);
}

void download_tile(int zoom, int x, int y)
{
	static struct addrinfo hints;
	struct addrinfo *res;
	int st, sock;

	printf("Preparing HTTP GET request for http://" SERVER_HOST ":%d/%d/%d/%d.png\n",
	       SERVER_PORT, zoom, x, y);

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	st = getaddrinfo(SERVER_HOST, "80", &hints, &res);
	printf("getaddrinfo status: %d\n", st);

	if (st != 0) {
		printf("Unable to resolve address, quitting\n");
		return 0;
	}

	dump_addrinfo(res);

	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	CHECK(sock);
	printf("sock = %d\n", sock);

	CHECK(connect(sock, res->ai_addr, res->ai_addrlen));

	freeaddrinfo(res);

	snprintf(buf, sizeof(buf), REQUEST_FMT, zoom, x, y);

	CHECK(send(sock, buf, strlen(buf), 0));

	printf("Response:\n\n");

	int response_length = 0;
	memset(full_response, 0, sizeof(full_response));
	while (1) {
		int r = recv(sock, buf, sizeof(buf), 0);
		if (r <= 0) {
			break;
		}

		// Ensure we don't overflow the full_response buffer
		if (response_length + r > sizeof(full_response)) {
			break;
		}

		// Copy the received data to the end of full_response
		memcpy(full_response + response_length, buf, r);
		response_length += r;
	}

	// Find the start of the payload
	char *payload = memmem(full_response, response_length, "\r\n\r\n", 4);
	if (payload != NULL) {
		// Skip the "\r\n\r\n"
		payload += 4;
	} else {
		// No payload found
		printf("No payload found in the response\n");
		return;
	}

	// Calculate the size of the payload
	int payload_size = full_response + response_length - payload;
	printf("Payload size: %d\n", payload_size);

	memset(png_raw, 0, sizeof(png_raw));
	memcpy(png_raw, payload, payload_size);

	my_png.data = (uint8_t *)png_raw;
	my_png.data_size = payload_size;

	lv_img_set_src(img, &my_png);

	printf("Image has been set successfully\n");

	printf("\n");

	(void)close(sock);
	return 0;
}

static void gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{
	int ret;
	const char *fmt = "navigation_data: {latitude: %s%lli.%09lli, longitude : %s%lli.%09lli, "
			  "bearing %u.%03u, speed %u.%03u, altitude: %s%i.%03i}";

	if (data->info.fix_status != GNSS_FIX_STATUS_NO_FIX) {
		// printf("%s has fix!\r\n", dev->name);

		struct navigation_data *nav_data = &data->nav_data;

		// printk("lon: %lld, lat: %lld, alt: %d\r\n", nav_data->longitude,
		//        nav_data->latitude, nav_data->altitude);

		// display tile URL
		int tile_x, tile_y;

		lat_lon_to_tile(nav_data->latitude / 1000000000.0,
				nav_data->longitude / 1000000000.0, current_zoom_level, &tile_x,
				&tile_y);

		static int counter = 0;
		if (counter++ % 4 == 0) {
			download_tile(current_zoom_level, tile_x, tile_y);
		}

		// display the tile
	}
}

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(gnss)), gnss_data_cb);

int main(void)
{
	const struct device *display_dev;
	lv_obj_t *hello_world_label;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		//		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	img = lv_img_create(lv_scr_act());
	// lv_img_set_src(img, &xx95963);
	lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

	// /* add zoom buttons */
	// btn_zoom_in = lv_btn_create(lv_scr_act());
	// lv_obj_set_size(btn_zoom_in, 80, 50);
	// lv_obj_align(btn_zoom_in, LV_ALIGN_BOTTOM_LEFT, 10, -10);
	// lv_obj_t *label1 = lv_label_create(btn_zoom_in);
	// lv_label_set_text(label1, "Zoom in");
	// lv_obj_add_event_cb(btn_zoom_in, zoom_in_out, LV_EVENT_CLICKED, NULL);

	// btn_zoom_out = lv_btn_create(lv_scr_act());
	// lv_obj_set_size(btn_zoom_out, 80, 50);
	// lv_obj_align(btn_zoom_out, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
	// lv_obj_t *label2 = lv_label_create(btn_zoom_out);
	// lv_label_set_text(label2, "Zoom out");
	// lv_obj_add_event_cb(btn_zoom_out, zoom_in_out, LV_EVENT_CLICKED, NULL);

	lv_task_handler();
	display_blanking_off(display_dev);

	while (1) {
		lv_task_handler();
		k_sleep(K_MSEC(10));
	}

	return 0;
}
