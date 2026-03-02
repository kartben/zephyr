/*
 * Copyright (c) 2022 Trackunit Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup modem
 * @brief UART backend helpers for modem pipes.
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/modem/pipe.h>
#include <zephyr/modem/stats.h>

#ifndef ZEPHYR_MODEM_BACKEND_UART_
#define ZEPHYR_MODEM_BACKEND_UART_

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL_HIDDEN */

/** @brief Internal state used by the IRQ-driven UART backend. */
struct modem_backend_uart_isr {
	struct ring_buf receive_rdb[2];
	struct ring_buf transmit_rb;
	atomic_t transmit_buf_len;
	atomic_t receive_buf_len;
	uint8_t receive_rdb_used;
	uint32_t transmit_buf_put_limit;
};

/** @brief Common internal state for asynchronous UART backends. */
struct modem_backend_uart_async_common {
	uint8_t *transmit_buf;
	uint32_t transmit_buf_size;
	struct k_work rx_disabled_work;
	atomic_t state;
};

#ifdef CONFIG_MODEM_BACKEND_UART_ASYNC_HWFC

/** @brief Internal RX queue event container. */
struct rx_queue_event {
	uint8_t *buf;
	size_t len;
};

/** @brief Internal asynchronous UART backend state with HW flow control. */
struct modem_backend_uart_async {
	struct modem_backend_uart_async_common common;
	struct k_mem_slab rx_slab;
	struct k_msgq rx_queue;
	struct rx_queue_event rx_event;
	struct rx_queue_event rx_queue_buf[CONFIG_MODEM_BACKEND_UART_ASYNC_HWFC_BUFFER_COUNT];
	uint32_t rx_buf_size;
	uint8_t rx_buf_count;
};

#else

/** @brief Internal asynchronous UART backend state without HW flow control. */
struct modem_backend_uart_async {
	struct modem_backend_uart_async_common common;
	uint8_t *receive_bufs[2];
	uint32_t receive_buf_size;
	struct ring_buf receive_rb;
	struct k_spinlock receive_rb_lock;
};

#endif /* CONFIG_MODEM_BACKEND_UART_ASYNC_HWFC */

/** @endcond */

/** @brief UART modem backend instance. */
struct modem_backend_uart {
	/** UART device used by the backend. */
	const struct device *uart;
	/** Optional DTR GPIO used by the backend. */
	const struct gpio_dt_spec *dtr_gpio;
	/** Modem pipe exposed by the backend. */
	struct modem_pipe pipe;
	/** Delayed work item used to notify receive readiness. */
	struct k_work_delayable receive_ready_work;
	/** Work item used to notify transmit idle events. */
	struct k_work transmit_idle_work;

#if CONFIG_MODEM_STATS
	/** Receive buffer statistics tracker. */
	struct modem_stats_buffer receive_buf_stats;
	/** Transmit buffer statistics tracker. */
	struct modem_stats_buffer transmit_buf_stats;
#endif

	/** @cond INTERNAL_HIDDEN */
	union {
		struct modem_backend_uart_isr isr;
		struct modem_backend_uart_async async;
	};
	/** @endcond */
};

/** @brief UART modem backend configuration. */
struct modem_backend_uart_config {
	/** UART device used by the backend. */
	const struct device *uart;
	/** Optional DTR GPIO used by the backend. */
	const struct gpio_dt_spec *dtr_gpio;
	/* Address must be word-aligned when CONFIG_MODEM_BACKEND_UART_ASYNC_HWFC is enabled. */
	/** Receive buffer storage. */
	uint8_t *receive_buf;
	/** Size of @ref receive_buf in bytes. */
	uint32_t receive_buf_size;
	/** Transmit buffer storage. */
	uint8_t *transmit_buf;
	/** Size of @ref transmit_buf in bytes. */
	uint32_t transmit_buf_size;
};

/**
 * @brief Initialize a UART modem backend.
 *
 * @param[in,out] backend Backend instance to initialize.
 * @param[in] config Backend configuration.
 *
 * @return Pointer to the modem pipe exposed by @p backend.
 * @retval NULL If backend initialization failed.
 */
struct modem_pipe *modem_backend_uart_init(struct modem_backend_uart *backend,
					   const struct modem_backend_uart_config *config);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODEM_BACKEND_UART_ */
