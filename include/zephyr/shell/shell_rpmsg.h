/*
 * Copyright (c) 2024 Basalte bv
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell RPMsg transport backend.
 */

#ifndef ZEPHYR_INCLUDE_SHELL_RPMSG_H_
#define ZEPHYR_INCLUDE_SHELL_RPMSG_H_

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <openamp/rpmsg.h>

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

extern const struct shell_transport_api shell_rpmsg_transport_api;

/**
 * @brief RPMsg received message placeholder.
 */
struct shell_rpmsg_rx {
	void *data;   /**< Pointer to the data held by RPMsg endpoint. */
	size_t len;   /**< The length of the data. */
};

/**
 * @brief RPMsg-based shell transport context.
 */
struct shell_rpmsg {
	shell_transport_handler_t shell_handler; /**< Handler function. */
	void *shell_context;                     /**< Context registered by shell. */
	bool ready;                              /**< Ready to read/write indicator. */
	bool blocking;                           /**< Blocking mode setting. */
	struct rpmsg_endpoint ept;               /**< RPMsg endpoint. */
	struct k_msgq rx_q;                      /**< Queue for received data. */
	struct shell_rpmsg_rx rx_buf[CONFIG_SHELL_RPMSG_MAX_RX]; /**< Received messages buffer. */
	struct shell_rpmsg_rx rx_cur;            /**< Current RX message. */
	size_t rx_consumed;                      /**< Bytes consumed from rx_cur. */
};

/**
 * @endcond
 */

/**
 * @brief Macro for creating shell RPMsg transport instance.
 *
 * @param _name Instance name.
 */
#define SHELL_RPMSG_DEFINE(_name)					\
	static struct shell_rpmsg _name##_shell_rpmsg;			\
	struct shell_transport _name = {				\
		.api = &shell_rpmsg_transport_api,			\
		.ctx = (struct shell_rpmsg *)&_name##_shell_rpmsg,	\
	}

/**
 * @brief Initialize the shell backend using the provided RPMsg device.
 *
 * @param rpmsg_dev Pointer to an RPMsg device.
 *
 * @return 0 on success or a negative value on error.
 */
int shell_backend_rpmsg_init_transport(struct rpmsg_device *rpmsg_dev);

/**
 * @brief Get pointer to the shell RPMsg backend instance.
 *
 * This function returns a pointer to the shell RPMsg instance. This instance
 * can be used with shell_execute_cmd() to test command behavior.
 *
 * @return Pointer to the shell instance.
 */
const struct shell *shell_backend_rpmsg_get_ptr(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_RPMSG_H_ */
