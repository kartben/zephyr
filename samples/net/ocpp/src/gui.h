/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GUI_H_
#define GUI_H_

enum gui_connector_state {
	GUI_CONNECTOR_IDLE,
	GUI_CONNECTOR_AUTHORIZING,
	GUI_CONNECTOR_CHARGING,
	GUI_CONNECTOR_ERROR,
};

/**
 * @brief Initialize the LVGL GUI
 *
 * @return 0 on success, negative errno on failure
 */
int gui_init(void);

/**
 * @brief Update the main status message
 *
 * @param status Status text to display
 */
void gui_update_status(const char *status);

/**
 * @brief Update the IP address display
 *
 * @param ip IP address string
 */
void gui_update_ip(const char *ip);

/**
 * @brief Update connector state
 *
 * @param connector_id Connector ID (1-2)
 * @param state New state
 */
void gui_update_connector_state(int connector_id, enum gui_connector_state state);

/**
 * @brief Update connector ID tag
 *
 * @param connector_id Connector ID (1-2)
 * @param idtag ID tag string
 */
void gui_update_connector_idtag(int connector_id, const char *idtag);

/**
 * @brief Update connector meter reading
 *
 * @param connector_id Connector ID (1-2)
 * @param watt_hours Meter value in watt-hours
 */
void gui_update_connector_meter(int connector_id, int watt_hours);

/**
 * @brief GUI task - must be called periodically
 *
 * This function runs the LVGL timer handler in a loop
 */
void gui_task(void);

#endif /* GUI_H_ */
