/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — secure telemetry reporting module.
 *
 * Transmits sensor readings over a WireGuard VPN tunnel to the facility
 * management system.  Requires CONFIG_WIREGUARD=y and the zephyr-wireguard
 * west module.  When CONFIG_WIREGUARD is not set the module compiles to
 * no-ops so the rest of the application still works on vanilla native_sim.
 */

#ifndef SECUREVAULT_REPORT_H
#define SECUREVAULT_REPORT_H

#include "monitor.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the reporting subsystem.
 *
 * Sets up the WireGuard virtual interface, installs the peer public key, and
 * assigns the tunnel IP address.  Must be called after the network stack is up.
 *
 * @return 0 on success, negative errno on failure (or always 0 if WireGuard
 *         is not compiled in).
 */
int report_init(void);

/**
 * @brief Send a telemetry report for the given sensor reading.
 *
 * Opens a UDP socket over the WireGuard tunnel interface, sends a compact
 * JSON payload, and closes the socket.  Socket cleanup is handled by
 * scope_defer — no manual close needed on error paths.
 *
 * @param reading  Latest sensor snapshot to report.
 * @param auth_events  Number of authentication events since last report.
 * @return 0 on success, negative errno on failure.
 */
int report_send(const struct monitor_reading *reading, uint32_t auth_events);

#ifdef __cplusplus
}
#endif

#endif /* SECUREVAULT_REPORT_H */
