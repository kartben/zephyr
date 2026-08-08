/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the thread-safe command line option parsing API (sys_getopt).
 * @ingroup utilities
 */

#ifndef ZEPHYR_INCLUDE_SYS_SYS_GETOPT_H_
#define ZEPHYR_INCLUDE_SYS_SYS_GETOPT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/kernel.h>

/**
 * @brief getopt parsing state
 *
 * State of command line argument parsing for one caller, as returned by
 * sys_getopt_state_get().
 */
struct sys_getopt_state {
	int opterr;   /**< If error message should be printed */
	int optind;   /**< Index into parent argv vector */
	int optopt;   /**< Character checked for validity */
	int optreset; /**< Reset getopt */
	char *optarg; /**< Argument associated with option */

	/** @cond INTERNAL_HIDDEN */
	char *place; /* option letter processing */

#if CONFIG_GETOPT_LONG
	int nonopt_start;
	int nonopt_end;
#endif
	/** @endcond */
};

/**
 * @name Global getopt variables
 *
 * Mirrors of the getopt state of the most recent sys_getopt() (or variant)
 * call. They are shared by all threads, so they only reflect the parsing
 * state when a single thread uses getopt; with multiple threads, use
 * sys_getopt_state_get() instead.
 * @{
 */
extern int sys_getopt_optreset; /**< Reset getopt */
extern char *sys_getopt_optarg; /**< Argument associated with option */
extern int sys_getopt_opterr;   /**< If error message should be printed */
extern int sys_getopt_optind;   /**< Index into parent argv vector */
extern int sys_getopt_optopt;   /**< Character checked for validity */
/** @} */

/** The long option takes no argument */
#define sys_getopt_no_argument       0
/** The long option requires an argument */
#define sys_getopt_required_argument 1
/** The long option takes an optional argument */
#define sys_getopt_optional_argument 2

/**
 * @brief Long option descriptor
 *
 * Describes a single long option for sys_getopt_long() and
 * sys_getopt_long_only().
 */
struct sys_getopt_option {
	/** Name of long option */
	const char *name;
	/**
	 * One of sys_getopt_no_argument, sys_getopt_required_argument or
	 * sys_getopt_optional_argument: whether option takes an argument
	 */
	int has_arg;
	/** If not NULL, set *flag to val when option found */
	int *flag;
	/** If flag not NULL, value to set *flag to; else return value */
	int val;
};

/** @brief Initialize the getopt state of the current thread. */
void sys_getopt_init(void);

/**
 * @brief Get the getopt state of the current thread.
 *
 * @return Pointer to the getopt state of the current thread.
 */
struct sys_getopt_state *sys_getopt_state_get(void);

/**
 * @brief Parses the command-line arguments.
 *
 * @note This function is based on FreeBSD implementation but it does not
 * support environment variable: POSIXLY_CORRECT.
 *
 * @param[in] nargc	   Arguments count.
 * @param[in] nargv	   Arguments.
 * @param[in] ostr	   String containing the legitimate option characters.
 *
 * @return		If an option was successfully found, function returns
 *			the option character.
 */
int sys_getopt(int nargc, char *const nargv[], const char *ostr);

/**
 * @brief Parses the command-line arguments.
 *
 * The sys_getopt_long() function works like @ref sys_getopt() except
 * it also accepts long options, started with two dashes.
 *
 * @note This function is based on FreeBSD implementation but it does not
 * support environment variable: POSIXLY_CORRECT.
 *
 * @param[in] nargc	   Arguments count.
 * @param[in] nargv	   Arguments.
 * @param[in] options	   String containing the legitimate option characters.
 * @param[in] long_options Pointer to the first element of an array of
 *			   @a struct sys_getopt_option.
 * @param[in] idx	   If idx is not NULL, it points to a variable
 *			   which is set to the index of the long option relative
 *			   to @p long_options.
 *
 * @return		If an option was successfully found, function returns
 *			the option character.
 */
int sys_getopt_long(int nargc, char *const *nargv, const char *options,
		    const struct sys_getopt_option *long_options, int *idx);

/**
 * @brief Parses the command-line arguments.
 *
 * The sys_getopt_long_only() function works like @ref sys_getopt_long(),
 * but '-' as well as "--" can indicate a long option. If an option that starts
 * with '-' (not "--") doesn't match a long option, but does match a short
 * option, it is parsed as a short option instead.
 *
 * @note This function is based on FreeBSD implementation but it does not
 * support environment variable: POSIXLY_CORRECT.
 *
 * @param[in] nargc	   Arguments count.
 * @param[in] nargv	   Arguments.
 * @param[in] options	   String containing the legitimate option characters.
 * @param[in] long_options Pointer to the first element of an array of
 *			   @a struct sys_getopt_option.
 * @param[in] idx	   If idx is not NULL, it points to a variable
 *			   which is set to the index of the long option relative
 *			   to @p long_options.
 *
 * @return		If an option was successfully found, function returns
 *			the option character.
 */
int sys_getopt_long_only(int nargc, char *const *nargv, const char *options,
			 const struct sys_getopt_option *long_options, int *idx);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_SYS_GETOPT_H_ */
