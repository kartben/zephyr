/*
 * Copyright (c) 2018 Makaio GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell RTT backend API.
 */

#ifndef ZEPHYR_INCLUDE_SHELL_RTT_H_
#define ZEPHYR_INCLUDE_SHELL_RTT_H_

#include <zephyr/shell/shell.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup shell_api
 * @{
 */

/**
 * @cond INTERNAL_HIDDEN
 */

extern const struct shell_transport_api shell_rtt_transport_api;

/**
 * @brief Shell RTT transport structure.
 */
struct shell_rtt {
	shell_transport_handler_t handler; /**< Event handler. */
	struct k_timer timer;              /**< RX timer. */
	void *context;                     /**< Handler context. */
};

/**
 * @brief Macro for creating shell RTT transport instance.
 *
 * @param _name Instance name.
 */
#define SHELL_RTT_DEFINE(_name)					\
	static struct shell_rtt _name##_shell_rtt;			\
	struct shell_transport _name = {				\
		.api = &shell_rtt_transport_api,			\
		.ctx = (struct shell_rtt *)&_name##_shell_rtt		\
	}

/**
 * @endcond
 */

/**
 * @brief Get pointer to shell RTT backend instance.
 *
 * Function returns pointer to the shell RTT instance. This instance can be
 * used with shell_execute_cmd function in order to test commands behavior.
 *
 * @return Pointer to the shell instance.
 */
const struct shell *shell_backend_rtt_get_ptr(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_RTT_H_ */
