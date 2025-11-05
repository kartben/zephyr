/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initialize GUI immediately at boot; returns 0 on success. */
int gui_init(void);

/* Status setters (thread-safe). These do not touch LVGL directly from caller threads. */
void gui_set_network_status(bool is_up);
void gui_set_ocpp_status(const char *status_text);

/* Update connector telemetry (thread-safe). id is 1-based connector index. */
void gui_update_connector(int id, uint32_t meter_wh, uint8_t soc_percent, int voltage_v,
			  int current_a, int power_w, bool is_charging);

/* Optional transient notification at the bottom of the screen. */
void gui_show_notification(const char *text);

/* Register a callback function to be called periodically (every 1s) for connector updates.
 * The callback should call gui_update_connector() for each connector that needs updating.
 * This allows the GUI to request updates independently of OCPP callbacks.
 */
typedef void (*gui_periodic_update_cb_t)(void);
void gui_register_periodic_update_callback(gui_periodic_update_cb_t cb);
