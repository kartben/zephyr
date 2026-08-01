/* printk.h - low-level debug output */

/*
 * Copyright (c) 2010-2012, 2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the low-level debug output (printk) APIs.
 * @ingroup cbprintf_apis
 */

#ifndef ZEPHYR_INCLUDE_SYS_PRINTK_H_
#define ZEPHYR_INCLUDE_SYS_PRINTK_H_

#include <zephyr/toolchain.h>
#include <stddef.h>
#include <stdarg.h>
#include <inttypes.h>

#ifdef CONFIG_LOG_PRINTK_STATIC
#include <zephyr/logging/log_core.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 *
 * @brief Print kernel debugging message.
 *
 * This routine prints a kernel debugging message to the system console.
 * Output is send immediately, without any mutual exclusion or buffering.
 *
 * A basic set of conversion specifier characters are supported:
 *   - signed decimal: \%d, \%i
 *   - unsigned decimal: \%u
 *   - unsigned hexadecimal: \%x (\%X is treated as \%x)
 *   - pointer: \%p
 *   - string: \%s
 *   - character: \%c
 *   - percent: \%\%
 *
 * Field width (with or without leading zeroes) is supported.
 * Length attributes h, hh, l, ll and z are supported. However, integral
 * values with %lld and %lli are only printed if they fit in a long
 * otherwise 'ERR' is printed. Full 64-bit values may be printed with %llx.
 * Flags and precision attributes are not supported.
 *
 * @param fmt Format string.
 * @param ... Optional list of format arguments.
 */
#ifdef CONFIG_PRINTK

#ifdef CONFIG_LOG_PRINTK_STATIC
/* If printk is redirected to the logging use the macro which allow build time
 * logging message creation which is faster and allows format string stripping in
 * case of dictionary based logging.
 */
#define printk(...) Z_LOG_PRINTK(0, __VA_ARGS__)
#else
__printf_like(1, 2) void printk(const char *fmt, ...);
#endif /* CONFIG_LOG_PRINTK_STATIC */

/**
 * @brief Print kernel debugging message using a va_list.
 *
 * Equivalent to printk() except that the format arguments are passed as a
 * @c va_list. See printk() for the supported conversion specifiers.
 *
 * @param fmt Format string.
 * @param ap Variable argument list.
 */
__printf_like(1, 0) void vprintk(const char *fmt, va_list ap);

#else
static inline __printf_like(1, 2) void printk(const char *fmt, ...)
{
	ARG_UNUSED(fmt);
}

/**
 * @brief Print kernel debugging message using a va_list.
 *
 * Equivalent to printk() except that the format arguments are passed as a
 * @c va_list. See printk() for the supported conversion specifiers.
 *
 * @param fmt Format string.
 * @param ap Variable argument list.
 */
static inline __printf_like(1, 0) void vprintk(const char *fmt, va_list ap)
{
	ARG_UNUSED(fmt);
	ARG_UNUSED(ap);
}
#endif

#ifdef CONFIG_PICOLIBC

#include <stdio.h>

#define snprintk(...) snprintf(__VA_ARGS__)
#define vsnprintk(str, size, fmt, ap) vsnprintf(str, size, fmt, ap)

#else

/**
 * @brief Write formatted output to a buffer.
 *
 * Equivalent to printk() except that the output is written to @p str, which
 * is null terminated and truncated to @p size characters (including the
 * terminating null byte). See printk() for the supported conversion
 * specifiers.
 *
 * @param str Output buffer.
 * @param size Size of the output buffer, in bytes.
 * @param fmt Format string.
 * @param ... Optional list of format arguments.
 *
 * @return Number of characters that would have been written to @p str,
 *         excluding the terminating null byte, had the buffer been large
 *         enough.
 */
__printf_like(3, 4) int snprintk(char *str, size_t size,
					const char *fmt, ...);

/**
 * @brief Write formatted output to a buffer using a va_list.
 *
 * Equivalent to snprintk() except that the format arguments are passed as a
 * @c va_list.
 *
 * @param str Output buffer.
 * @param size Size of the output buffer, in bytes.
 * @param fmt Format string.
 * @param ap Variable argument list.
 *
 * @return Number of characters that would have been written to @p str,
 *         excluding the terminating null byte, had the buffer been large
 *         enough.
 */
__printf_like(3, 0) int vsnprintk(char *str, size_t size,
					  const char *fmt, va_list ap);

#endif

#ifdef __cplusplus
}
#endif

#endif
