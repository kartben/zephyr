/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell fprintf internal API.
 */

#ifndef ZEPHYR_INCLUDE_SHELL_FPRINTF_H_
#define ZEPHYR_INCLUDE_SHELL_FPRINTF_H_

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @cond INTERNAL_HIDDEN
 */

/**
 * @brief Shell fprintf write callback type.
 *
 * @param user_ctx User context.
 * @param data Data to write.
 * @param length Length of data.
 */
typedef void (*shell_fprintf_fwrite)(const void *user_ctx,
				     const char *data,
				     size_t length);

/**
 * @brief Shell fprintf control block structure.
 */
struct shell_fprintf_control_block {
	size_t buffer_cnt; /**< Buffer byte count. */
	bool autoflush;    /**< Autoflush enabled. */
};

/**
 * @brief Shell fprintf context structure.
 */
struct shell_fprintf {
	uint8_t *buffer;                            /**< Output buffer. */
	size_t buffer_size;                         /**< Output buffer size. */
	shell_fprintf_fwrite fwrite;                /**< Write callback. */
	const void *user_ctx;                       /**< User context. */
	struct shell_fprintf_control_block *ctrl_blk; /**< Control block. */
};

/**
 * @brief Macro for defining shell_fprintf instance.
 *
 * @param _name		Instance name.
 * @param _user_ctx	Pointer to user data.
 * @param _buf		Pointer to output buffer
 * @param _size		Size of output buffer.
 * @param _autoflush	Indicator if buffer shall be automatically flush.
 * @param _fwrite	Pointer to function sending data stream.
 */
#define Z_SHELL_FPRINTF_DEFINE(_name, _user_ctx, _buf, _size,	\
			    _autoflush, _fwrite)		\
	static struct shell_fprintf_control_block		\
				_name##_shell_fprintf_ctx = {	\
		.autoflush = _autoflush,			\
		.buffer_cnt = 0					\
	};							\
	static const struct shell_fprintf _name = {		\
		.buffer = _buf,					\
		.buffer_size = _size,				\
		.fwrite = _fwrite,				\
		.user_ctx = _user_ctx,				\
		.ctrl_blk = &_name##_shell_fprintf_ctx		\
	}

/**
 * @brief fprintf like function which sends formatted data stream to output.
 *
 * @param sh_fprintf	fprintf instance.
 * @param fmt		Format string.
 * @param args		List of parameters to print.
 */
void z_shell_fprintf_fmt(const struct shell_fprintf *sh_fprintf,
			 char const *fmt, va_list args);

/**
 * @brief Function flushing data stored in io_buffer.
 *
 * @param sh_fprintf	fprintf instance.
 */
void z_shell_fprintf_buffer_flush(const struct shell_fprintf *sh_fprintf);

/**
 * @endcond
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_FPRINTF_H_ */
