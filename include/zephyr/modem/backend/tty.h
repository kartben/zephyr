/*
 * Copyright (c) 2022 Trackunit Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup modem
 * @brief TTY backend helpers for modem pipes.
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/modem/pipe.h>

#ifndef ZEPHYR_MODEM_BACKEND_TTY_
#define ZEPHYR_MODEM_BACKEND_TTY_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief TTY modem backend instance. */
struct modem_backend_tty {
	/** Path to the TTY device node. */
	const char *tty_path;
	/** @cond INTERNAL_HIDDEN */
	int tty_fd;
	/** @endcond */
	/** Modem pipe exposed by the backend. */
	struct modem_pipe pipe;
	/** @cond INTERNAL_HIDDEN */
	struct k_thread thread;
	/** @endcond */
	/** Thread stack storage used by the polling thread. */
	k_thread_stack_t *stack;
	/** Size of @ref stack in bytes. */
	size_t stack_size;
	/** @cond INTERNAL_HIDDEN */
	atomic_t state;
	/** @endcond */
};

/** @brief TTY modem backend configuration. */
struct modem_backend_tty_config {
	/** Path to the TTY device node. */
	const char *tty_path;
	/** Stack storage used by the backend polling thread. */
	k_thread_stack_t *stack;
	/** Size of @ref stack in bytes. */
	size_t stack_size;
};

/**
 * @brief Initialize a TTY modem backend.
 *
 * @param[in,out] backend Backend instance to initialize.
 * @param[in] config Backend configuration.
 *
 * @return Pointer to the modem pipe exposed by @p backend.
 */
struct modem_pipe *modem_backend_tty_init(struct modem_backend_tty *backend,
					  const struct modem_backend_tty_config *config);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODEM_BACKEND_TTY_ */
