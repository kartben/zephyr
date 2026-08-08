/*
 * Copyright (c) 2022 Trackunit Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the TTY modem pipe backend.
 * @ingroup modem
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/modem/pipe.h>

#ifndef ZEPHYR_INCLUDE_MODEM_BACKEND_TTY_H_
#define ZEPHYR_INCLUDE_MODEM_BACKEND_TTY_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TTY backend instance internal context
 * @warning Do not modify any members of this struct directly
 */
struct modem_backend_tty {
	/** @cond INTERNAL_HIDDEN */
	const char *tty_path;
	int tty_fd;
	struct modem_pipe pipe;
	struct k_thread thread;
	k_thread_stack_t *stack;
	size_t stack_size;
	atomic_t state;
	/** @endcond */
};

/** TTY backend configuration */
struct modem_backend_tty_config {
	/** Path to the TTY device */
	const char *tty_path;
	/** Stack used by the backend thread */
	k_thread_stack_t *stack;
	/** Size of the stack used by the backend thread */
	size_t stack_size;
};

/**
 * @brief Initialize a TTY backend
 *
 * @param backend TTY backend instance
 * @param config Configuration to apply to the backend instance
 *
 * @return Pointer to the modem pipe of the backend
 */
struct modem_pipe *modem_backend_tty_init(struct modem_backend_tty *backend,
					  const struct modem_backend_tty_config *config);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MODEM_BACKEND_TTY_H_ */
