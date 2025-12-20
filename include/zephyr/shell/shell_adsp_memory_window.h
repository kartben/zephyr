/*
 * Copyright (c) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell ADSP memory window backend API.
 */

#ifndef SHELL_ADSP_MEMORY_WINDOW_H__
#define SHELL_ADSP_MEMORY_WINDOW_H__

#include <zephyr/kernel.h>
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

extern const struct shell_transport_api shell_adsp_memory_window_transport_api;

struct sys_winstream;

/**
 * @brief Memory window based shell transport structure.
 */
struct shell_adsp_memory_window {
	shell_transport_handler_t shell_handler; /**< Handler function registered by shell. */
	struct k_timer timer;                    /**< Timer for polling. */
	void *shell_context;                     /**< Context registered by shell. */
	struct sys_winstream *ws_rx;             /**< Receive winstream object. */
	struct sys_winstream *ws_tx;             /**< Transmit winstream object. */
	uint32_t read_seqno;                     /**< Last read sequence number. */
};

/**
 * @brief Macro for creating shell ADSP memory window transport instance.
 *
 * @param _name Instance name.
 */
#define SHELL_ADSP_MEMORY_WINDOW_DEFINE(_name)				\
	static struct shell_adsp_memory_window _name##_shell_adsp_memory_window;\
	struct shell_transport _name = {					\
		.api = &shell_adsp_memory_window_transport_api,		\
		.ctx = &_name##_shell_adsp_memory_window,	\
	}

/**
 * @endcond
 */

/**
 * @brief Get pointer to shell ADSP memory window backend instance.
 *
 * Function returns pointer to the shell ADSP memory window instance. This instance can be
 * used with shell_execute_cmd function in order to test commands behavior.
 *
 * @return Pointer to the shell instance.
 */
const struct shell *shell_backend_adsp_memory_window_get_ptr(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* SHELL_ADSP_MEMORY_WINDOW_H__ */
