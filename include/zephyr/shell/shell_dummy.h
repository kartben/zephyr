/*
 * Shell backend used for testing
 *
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell dummy transport backend for testing.
 */

#ifndef ZEPHYR_INCLUDE_SHELL_DUMMY_H_
#define ZEPHYR_INCLUDE_SHELL_DUMMY_H_

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

extern const struct shell_transport_api shell_dummy_transport_api;

/**
 * @brief Shell dummy transport context.
 */
struct shell_dummy {
	bool initialized; /**< Initialization flag. */
	size_t len;       /**< Current number of bytes in buffer. */
	char buf[CONFIG_SHELL_BACKEND_DUMMY_BUF_SIZE]; /**< Output buffer. */
};

/**
 * @endcond
 */

/**
 * @brief Macro for creating shell dummy transport instance.
 *
 * @param _name Instance name.
 */
#define SHELL_DUMMY_DEFINE(_name)					\
	static struct shell_dummy _name##_shell_dummy;			\
	struct shell_transport _name = {				\
		.api = &shell_dummy_transport_api,			\
		.ctx = (struct shell_dummy *)&_name##_shell_dummy	\
	}

/**
 * @brief Get pointer to the shell dummy backend instance.
 *
 * This function returns a pointer to the shell dummy instance. This instance
 * can be used with shell_execute_cmd() to test command behavior.
 *
 * @note This function should not be used directly. Use shell_execute_cmd()
 *       for testing purposes.
 *
 * @return Pointer to the shell instance.
 */
const struct shell *shell_backend_dummy_get_ptr(void);

/**
 * @brief Get buffered output from the shell dummy backend.
 *
 * Returns the buffered output in the shell and resets the pointer.
 * The returned data is always followed by a nul character at position *sizep.
 *
 * @param sh    Shell pointer.
 * @param sizep Returns size of data in shell buffer.
 *
 * @return Pointer to buffer containing shell output.
 */
const char *shell_backend_dummy_get_output(const struct shell *sh,
					   size_t *sizep);

/**
 * @brief Clear the output buffer in the shell dummy backend.
 *
 * @param sh Shell pointer.
 */
void shell_backend_dummy_clear_output(const struct shell *sh);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_DUMMY_H_ */
