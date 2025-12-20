/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell UART transport backend.
 */

#ifndef ZEPHYR_INCLUDE_SHELL_UART_H_
#define ZEPHYR_INCLUDE_SHELL_UART_H_

#include <zephyr/drivers/serial/uart_async_rx.h>
#include <zephyr/mgmt/mcumgr/transport/smp_shell.h>
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

extern const struct shell_transport_api shell_uart_transport_api;

#ifndef CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE
#define CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE 0
#endif

#ifndef CONFIG_SHELL_BACKEND_SERIAL_TX_RING_BUFFER_SIZE
#define CONFIG_SHELL_BACKEND_SERIAL_TX_RING_BUFFER_SIZE 0
#endif

#ifndef CONFIG_SHELL_BACKEND_SERIAL_ASYNC_RX_BUFFER_COUNT
#define CONFIG_SHELL_BACKEND_SERIAL_ASYNC_RX_BUFFER_COUNT 0
#endif

#ifndef CONFIG_SHELL_BACKEND_SERIAL_ASYNC_RX_BUFFER_SIZE
#define CONFIG_SHELL_BACKEND_SERIAL_ASYNC_RX_BUFFER_SIZE 0
#endif

#define ASYNC_RX_BUF_SIZE (CONFIG_SHELL_BACKEND_SERIAL_ASYNC_RX_BUFFER_COUNT * \
		(CONFIG_SHELL_BACKEND_SERIAL_ASYNC_RX_BUFFER_SIZE + \
		 UART_ASYNC_RX_BUF_OVERHEAD))

/**
 * @brief Common UART shell transport data.
 */
struct shell_uart_common {
	const struct device *dev;          /**< UART device. */
	shell_transport_handler_t handler; /**< Transport handler. */
	void *context;                     /**< Handler context. */
	bool blocking_tx;                  /**< Blocking TX mode. */
#ifdef CONFIG_MCUMGR_TRANSPORT_SHELL
	struct smp_shell_data smp;         /**< SMP shell data. */
#endif /* CONFIG_MCUMGR_TRANSPORT_SHELL */
};

/**
 * @brief Interrupt-driven UART shell transport data.
 */
struct shell_uart_int_driven {
	struct shell_uart_common common;   /**< Common data. */
	struct ring_buf tx_ringbuf;        /**< TX ring buffer. */
	struct ring_buf rx_ringbuf;        /**< RX ring buffer. */
	uint8_t tx_buf[CONFIG_SHELL_BACKEND_SERIAL_TX_RING_BUFFER_SIZE]; /**< TX buffer. */
	uint8_t rx_buf[CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE]; /**< RX buffer. */
	struct k_timer dtr_timer;          /**< DTR timer. */
	atomic_t tx_busy;                  /**< TX busy flag. */
};

/**
 * @brief Async UART shell transport data.
 */
struct shell_uart_async {
	struct shell_uart_common common;          /**< Common data. */
	struct k_sem tx_sem;                      /**< TX semaphore. */
	struct uart_async_rx async_rx;            /**< Async RX context. */
	struct uart_async_rx_config async_rx_config; /**< Async RX config. */
	atomic_t pending_rx_req;                  /**< Pending RX request flag. */
	uint8_t rx_data[ASYNC_RX_BUF_SIZE];       /**< RX data buffer. */
};

/**
 * @brief Polling UART shell transport data.
 */
struct shell_uart_polling {
	struct shell_uart_common common;   /**< Common data. */
	struct ring_buf rx_ringbuf;        /**< RX ring buffer. */
	uint8_t rx_buf[CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE]; /**< RX buffer. */
	struct k_timer rx_timer;           /**< RX polling timer. */
};

#ifdef CONFIG_SHELL_BACKEND_SERIAL_API_POLLING
#define SHELL_UART_STRUCT struct shell_uart_polling
#elif defined(CONFIG_SHELL_BACKEND_SERIAL_API_ASYNC)
#define SHELL_UART_STRUCT struct shell_uart_async
#else
#define SHELL_UART_STRUCT struct shell_uart_int_driven
#endif

/**
 * @endcond
 */

/**
 * @brief Macro for creating shell UART transport instance.
 *
 * @param _name Instance name.
 * @param ...   Additional arguments (accepted but ignored for compatibility).
 *
 * @note Additional arguments are accepted (but ignored) for compatibility with
 * previous Zephyr versions and will be removed in future releases.
 */
#define SHELL_UART_DEFINE(_name, ...)                                                              \
	static SHELL_UART_STRUCT _name##_shell_uart;                                               \
	struct shell_transport _name = {                                                           \
		.api = &shell_uart_transport_api,                                                  \
		.ctx = (struct shell_telnet *)&_name##_shell_uart,                                 \
	}

/**
 * @brief Get pointer to the shell UART backend instance.
 *
 * This function returns a pointer to the shell UART instance. This instance
 * can be used with shell_execute_cmd() to test command behavior.
 *
 * @return Pointer to the shell instance.
 */
const struct shell *shell_backend_uart_get_ptr(void);

/**
 * @brief Get pointer to the SMP shell data of the UART shell transport.
 *
 * @return Pointer to the SMP shell data.
 */
struct smp_shell_data *shell_uart_smp_shell_data_get_ptr(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_UART_H_ */
