/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OCPP_GUI_H
#define OCPP_GUI_H

#include <zephyr/logging/log.h>

/**
 * @brief Initialize the OCPP GUI
 */
void ocpp_gui_init(void);

/**
 * @brief Update Central System connection status
 * @param connected true if connected, false otherwise
 */
void ocpp_gui_update_cs_status(bool connected);

/**
 * @brief Update connector status
 * @param connector_id Connector ID (1 or 2)
 * @param status Status string (e.g., "Available", "Charging", "Preparing")
 */
void ocpp_gui_update_connector_status(int connector_id, const char *status);

/**
 * @brief Update connector energy reading
 * @param connector_id Connector ID (1 or 2)
 * @param energy_wh Energy in Wh
 */
void ocpp_gui_update_connector_energy(int connector_id, int energy_wh);

/**
 * @brief Update connector power reading
 * @param connector_id Connector ID (1 or 2)
 * @param power_w Power in W
 */
void ocpp_gui_update_connector_power(int connector_id, int power_w);

/**
 * @brief Update general status message
 * @param status_text Status text to display
 */
void ocpp_gui_update_status(const char *status_text);

/**
 * @brief Process LVGL timer tasks - should be called periodically
 */
void ocpp_gui_task(void);

#endif /* OCPP_GUI_H */
