/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell Websocket backend API.
 */

#ifndef ZEPHYR_INCLUDE_SHELL_WEBSOCKET_H_
#define ZEPHYR_INCLUDE_SHELL_WEBSOCKET_H_

#include <zephyr/net/socket.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
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

#define SHELL_WEBSOCKET_SERVICE_COUNT CONFIG_SHELL_WEBSOCKET_BACKEND_COUNT

/**
 * @brief Websocket line buffer structure.
 */
struct shell_websocket_line_buf {
	char buf[CONFIG_SHELL_WEBSOCKET_LINE_BUF_SIZE]; /**< Line buffer. */
	uint16_t len;                                    /**< Current line length. */
};

/**
 * @brief WEBSOCKET-based shell transport structure.
 */
struct shell_websocket {
	shell_transport_handler_t shell_handler; /**< Handler function registered by shell. */
	void *shell_context;                     /**< Context registered by shell. */
	struct shell_websocket_line_buf line_out; /**< Buffer for outgoing line. */
	struct zsock_pollfd fds[1];              /**< Array for sockets. */
	uint8_t rx_buf[CONFIG_SHELL_CMD_BUFF_SIZE]; /**< Input buffer. */
	size_t rx_len;                           /**< Number of data bytes in input buffer. */
	struct k_mutex rx_lock;                  /**< Mutex protecting the input buffer access. */
	struct k_work_delayable send_work;       /**< Delayed work for sending data. */
	struct k_work_sync work_sync;            /**< Work sync. */
	bool output_lock;                        /**< If set, no output is sent to client. */
};

extern const struct shell_transport_api shell_websocket_transport_api;

/**
 * @brief Websocket setup function.
 *
 * @param ws_socket Websocket socket.
 * @param request_ctx HTTP request context.
 * @param user_data User data.
 *
 * @return 0 on success, negative errno on failure.
 */
int shell_websocket_setup(int ws_socket, struct http_request_ctx *request_ctx, void *user_data);

/**
 * @brief Enable websocket shell.
 *
 * @param sh Shell instance.
 *
 * @return 0 on success, negative errno on failure.
 */
int shell_websocket_enable(const struct shell *sh);

#define GET_WS_NAME(_service) ws_ctx_##_service
#define GET_WS_SHELL_NAME(_name) shell_websocket_##_name
#define GET_WS_TRANSPORT_NAME(_service) transport_shell_ws_##_service
#define GET_WS_DETAIL_NAME(_service) ws_res_detail_##_service

/**
 * @brief Macro for defining shell Websocket transport instance.
 *
 * @param _service HTTP service name.
 */
#define SHELL_WEBSOCKET_DEFINE(_service)					\
	static struct shell_websocket GET_WS_NAME(_service);			\
	static struct shell_transport GET_WS_TRANSPORT_NAME(_service) = {	\
		.api = &shell_websocket_transport_api,				\
		.ctx = &GET_WS_NAME(_service),					\
	}

#define SHELL_WS_PORT_NAME(_service)	http_service_##_service
#define SHELL_WS_BUF_NAME(_service)	ws_recv_buffer_##_service
#define SHELL_WS_TEMP_RECV_BUF_SIZE 256

/**
 * @brief Macro for defining Websocket HTTP service.
 *
 * @param _service HTTP service name.
 */
#define DEFINE_WEBSOCKET_HTTP_SERVICE(_service)					\
	uint8_t SHELL_WS_BUF_NAME(_service)[SHELL_WS_TEMP_RECV_BUF_SIZE];	\
	struct http_resource_detail_websocket					\
	    GET_WS_DETAIL_NAME(_service) = {					\
		.common = {							\
			.type = HTTP_RESOURCE_TYPE_WEBSOCKET,			\
										\
			/* We need HTTP/1.1 GET method for upgrading */		\
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),	\
		},								\
		.cb = shell_websocket_setup,					\
		.data_buffer = SHELL_WS_BUF_NAME(_service),			\
		.data_buffer_len = sizeof(SHELL_WS_BUF_NAME(_service)),		\
		.user_data = &GET_WS_NAME(_service),				\
	};									\
	HTTP_RESOURCE_DEFINE(ws_resource_##_service, _service,			\
			     "/" CONFIG_SHELL_WEBSOCKET_ENDPOINT_URL,		\
			     &GET_WS_DETAIL_NAME(_service))

/**
 * @brief Macro for defining Websocket service.
 *
 * @param _service HTTP service name.
 */
#define DEFINE_WEBSOCKET_SERVICE(_service)					\
	SHELL_WEBSOCKET_DEFINE(_service);					\
	SHELL_DEFINE(shell_websocket_##_service,				\
		     CONFIG_SHELL_WEBSOCKET_PROMPT,				\
		     &GET_WS_TRANSPORT_NAME(_service),				\
		     CONFIG_SHELL_WEBSOCKET_LOG_MESSAGE_QUEUE_SIZE,		\
		     CONFIG_SHELL_WEBSOCKET_LOG_MESSAGE_QUEUE_TIMEOUT,		\
		     SHELL_FLAG_OLF_CRLF);					\
	DEFINE_WEBSOCKET_HTTP_SERVICE(_service)

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
/**
 * @brief Macro for defining secure Websocket console.
 *
 * Use a secure connection only for Websocket.
 *
 * @param _service HTTP service name.
 * @param _sec_tag_list Security tag list.
 * @param _sec_tag_list_size Security tag list size.
 */
#define WEBSOCKET_CONSOLE_DEFINE(_service, _sec_tag_list, _sec_tag_list_size) \
	static uint16_t SHELL_WS_PORT_NAME(_service) =			  \
		CONFIG_SHELL_WEBSOCKET_PORT;				  \
	HTTPS_SERVICE_DEFINE(_service,					  \
			     CONFIG_SHELL_WEBSOCKET_IP_ADDR,		  \
			     &SHELL_WS_PORT_NAME(_service),		  \
			     SHELL_WEBSOCKET_SERVICE_COUNT,		  \
			     SHELL_WEBSOCKET_SERVICE_COUNT,		  \
			     NULL,					  \
			     NULL,                                        \
			     NULL,                                        \
			     _sec_tag_list,				  \
			     _sec_tag_list_size);			  \
	DEFINE_WEBSOCKET_SERVICE(_service);				  \


#else /* CONFIG_NET_SOCKETS_SOCKOPT_TLS */
/**
 * @brief Macro for defining Websocket console.
 *
 * TLS not possible so define only normal HTTP service.
 *
 * @param _service HTTP service name.
 * @param _sec_tag_list Security tag list (ignored).
 * @param _sec_tag_list_size Security tag list size (ignored).
 */
#define WEBSOCKET_CONSOLE_DEFINE(_service, _sec_tag_list, _sec_tag_list_size) \
	static uint16_t SHELL_WS_PORT_NAME(_service) =			\
		CONFIG_SHELL_WEBSOCKET_PORT;				\
	HTTP_SERVICE_DEFINE(_service,					\
			    CONFIG_SHELL_WEBSOCKET_IP_ADDR,		\
			    &SHELL_WS_PORT_NAME(_service),		\
			    SHELL_WEBSOCKET_SERVICE_COUNT,		\
			    SHELL_WEBSOCKET_SERVICE_COUNT,		\
			    NULL, NULL, NULL);				\
	DEFINE_WEBSOCKET_SERVICE(_service)

#endif /* CONFIG_NET_SOCKETS_SOCKOPT_TLS */

/**
 * @brief Macro for enabling Websocket console.
 *
 * @param _service HTTP service name.
 */
#define WEBSOCKET_CONSOLE_ENABLE(_service)				\
	(void)shell_websocket_enable(&GET_WS_SHELL_NAME(_service))

/**
 * @endcond
 */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_WEBSOCKET_H_ */
