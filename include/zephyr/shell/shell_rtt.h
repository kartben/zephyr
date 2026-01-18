/*
 * Copyright (c) 2018 Makaio GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell RTT transport backend.
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
 * @brief Shell RTT transport context.
 */
struct shell_rtt {
	shell_transport_handler_t handler; /**< Transport handler. */
	struct k_timer timer;              /**< Polling timer. */
	void *context;                     /**< Handler context. */
};

/**
 * @endcond
 */

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
 * @brief Get pointer to the shell RTT backend instance.
 *
 * This function returns a pointer to the shell RTT instance. This instance
 * can be used with shell_execute_cmd() to test command behavior.
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
