/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell Telnet backend API.
 */

#ifndef ZEPHYR_INCLUDE_SHELL_TELNET_H_
#define ZEPHYR_INCLUDE_SHELL_TELNET_H_

#include <zephyr/net/socket.h>
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

extern const struct shell_transport_api shell_telnet_transport_api;

#define SHELL_TELNET_POLLFD_COUNT 3
#define SHELL_TELNET_MAX_CMD_SIZE 3

/**
 * @brief Telnet line buffer structure.
 */
struct shell_telnet_line_buf {
	char buf[CONFIG_SHELL_TELNET_LINE_BUF_SIZE]; /**< Line buffer. */
	uint16_t len;                                 /**< Current line length. */
};

/**
 * @brief TELNET-based shell transport structure.
 */
struct shell_telnet {
	shell_transport_handler_t shell_handler; /**< Handler function registered by shell. */
	void *shell_context;                     /**< Context registered by shell. */
	struct shell_telnet_line_buf line_out;   /**< Buffer for outgoing line. */
	struct zsock_pollfd fds[SHELL_TELNET_POLLFD_COUNT]; /**< Array for sockets. */
	uint8_t rx_buf[CONFIG_SHELL_CMD_BUFF_SIZE]; /**< Input buffer. */
	size_t rx_len;                            /**< Number of data bytes within input buffer. */
	struct k_mutex rx_lock;                   /**< Mutex protecting the input buffer access. */
	uint8_t cmd_buf[SHELL_TELNET_MAX_CMD_SIZE]; /**< Command buffer. */
	uint8_t cmd_len;                          /**< Command length. */
	struct k_work_delayable send_work;        /**< Delayed work for sending data. */
	struct k_work_sync work_sync;             /**< Work sync. */
	bool output_lock;                         /**< If set, no output is sent to client. */
};

/**
 * @brief Macro for creating shell Telnet transport instance.
 *
 * @param _name Instance name.
 */
#define SHELL_TELNET_DEFINE(_name)					\
	static struct shell_telnet _name##_shell_telnet;		\
	struct shell_transport _name = {				\
		.api = &shell_telnet_transport_api,			\
		.ctx = (struct shell_telnet *)&_name##_shell_telnet	\
	}

/**
 * @endcond
 */

/**
 * @brief Get pointer to shell Telnet backend instance.
 *
 * Function returns pointer to the shell Telnet instance. This instance can be
 * used with shell_execute_cmd function in order to test commands behavior.
 *
 * @return Pointer to the shell instance.
 */
const struct shell *shell_backend_telnet_get_ptr(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_TELNET_H_ */
