/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell internal types
 */

#ifndef ZEPHYR_INCLUDE_SHELL_TYPES_H_
#define ZEPHYR_INCLUDE_SHELL_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief VT100 color codes for shell output.
 */
enum shell_vt100_color {
	SHELL_VT100_COLOR_BLACK,   /**< Black color. */
	SHELL_VT100_COLOR_RED,     /**< Red color. */
	SHELL_VT100_COLOR_GREEN,   /**< Green color. */
	SHELL_VT100_COLOR_YELLOW,  /**< Yellow color. */
	SHELL_VT100_COLOR_BLUE,    /**< Blue color. */
	SHELL_VT100_COLOR_MAGENTA, /**< Magenta color. */
	SHELL_VT100_COLOR_CYAN,    /**< Cyan color. */
	SHELL_VT100_COLOR_WHITE,   /**< White color. */
	SHELL_VT100_COLOR_DEFAULT, /**< Default terminal color. */
	/** @cond INTERNAL_HIDDEN */
	VT100_COLOR_END
	/** @endcond */
};

/**
 * @cond INTERNAL_HIDDEN
 */

/**
 * @brief VT100 color pair structure.
 */
struct shell_vt100_colors {
	enum shell_vt100_color col;   /**< Text color. */
	enum shell_vt100_color bgcol; /**< Background color. */
};

/**
 * @brief Shell multiline console context.
 */
struct shell_multiline_cons {
	uint16_t cur_x;        /**< Horizontal cursor position in edited command line. */
	uint16_t cur_x_end;    /**< Horizontal cursor position at the end of command. */
	uint16_t cur_y;        /**< Vertical cursor position in edited command. */
	uint16_t cur_y_end;    /**< Vertical cursor position at the end of command. */
	uint16_t terminal_hei; /**< Terminal screen height. */
	uint16_t terminal_wid; /**< Terminal screen width. */
	uint8_t name_len;      /**< Console name length. */
};

/**
 * @brief Shell VT100 context.
 */
struct shell_vt100_ctx {
	struct shell_multiline_cons cons; /**< Multiline console context. */
	struct shell_vt100_colors col;    /**< Current color settings. */
	uint16_t printed_cmd;             /**< Printed commands counter. */
};

/**
 * @endcond
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_TYPES_H_ */
