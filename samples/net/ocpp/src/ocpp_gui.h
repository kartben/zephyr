/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OCPP_GUI_H_
#define OCPP_GUI_H_

#include <zephyr/kernel.h>

/**
 * @brief Initialize the OCPP GUI
 * 
 * This function sets up the LVGL display and creates all GUI elements.
 */
void ocpp_gui_init(void);

/**
 * @brief Update the system status display
 * 
 * @param status Status text to display
 * @param connected Whether the system is connected to the central system
 */
void ocpp_gui_update_system_status(const char *status, bool connected);

/**
 * @brief Update the network status display
 * 
 * @param ip IP address string or NULL
 * @param connected Whether network is connected
 */
void ocpp_gui_update_network_status(const char *ip, bool connected);

/**
 * @brief Update a connector's status and readings
 * 
 * @param connector_id Connector ID (1 or 2)
 * @param status Status text (e.g., "Idle", "Charging", "Authorizing")
 * @param power_w Power in watts
 * @param energy_wh Energy in watt-hours
 * @param id_tag User ID tag (can be NULL)
 */
void ocpp_gui_update_connector(int connector_id, const char *status, 
                                uint32_t power_w, uint32_t energy_wh,
                                const char *id_tag);

/**
 * @brief Update the time display
 * 
 * @param timestamp Unix timestamp
 */
void ocpp_gui_update_time(uint32_t timestamp);

/**
 * @brief Process LVGL tasks (call periodically)
 */
void ocpp_gui_task(void);

#endif /* OCPP_GUI_H_ */
