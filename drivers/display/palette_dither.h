/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_DISPLAY_PALETTE_DITHER_H_
#define ZEPHYR_DRIVERS_DISPLAY_PALETTE_DITHER_H_

/*
 * Helper that lets palette-backed I_4 display drivers transparently accept RGB565/RGB888 writes.
 *
 * Drivers tap in with:
 *
 * @code{.c}
 *   #include "palette_dither.h"
 *
 *   static int foo_write(dev, x, y, desc, buf) {
 *           DISPLAY_PALETTE_DITHER_PREPROCESS(dev, desc, buf);
 *           // ...existing I_4-only body, unchanged...
 *   }
 *
 *   static void foo_get_capabilities(dev, caps) {
 *           // ...existing population of caps (advertising I_4 only, in most cases)...
 *           DISPLAY_PALETTE_DITHER_PATCH_CAPS(dev, caps); // ORs in the helper's extra supported
 *                                                         // formats
 *   }
 *
 *   static int foo_set_pixel_format(dev, pf) {
 *           if (pf == PIXEL_FORMAT_I_4) {
 *                   // In case driver handles I_4 natively, passthrough
 *                   DISPLAY_PALETTE_DITHER_PASSTHROUGH(dev);
 *                   return 0;
 *           }
 *           // Otherwise, ask the helper to take ownership of this format
 *           return DISPLAY_PALETTE_DITHER_CLAIM(dev, pf);
 *   }
 *
 *   // In the driver's DT_INST_DEFINE() macro, add the below so that the helper can find its
 *   // per-instance state.
 *   #define FOO_DEFINE(inst)                                                  \
 *           DISPLAY_PALETTE_DITHER_INST_DEFINE(foo, inst);                    \
 *           static const struct foo_config foo_cfg_##inst = { ... };          \
 *           ...
 * @endcode
 *
 * All macros are no-ops when @kconfig{CONFIG_DISPLAY_PALETTE_DITHER} is disabled.
 */

#include <errno.h>

#include <zephyr/drivers/display.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_DISPLAY_PALETTE_DITHER

#if defined(CONFIG_DISPLAY_PALETTE_DITHER_DEFAULT_RGB888)
#define _PDITHER_DEFAULT_FMT PIXEL_FORMAT_RGB_888
#elif defined(CONFIG_DISPLAY_PALETTE_DITHER_DEFAULT_RGB565)
#define _PDITHER_DEFAULT_FMT PIXEL_FORMAT_RGB_565
#else
#define _PDITHER_DEFAULT_FMT PIXEL_FORMAT_I_4
#endif

struct display_palette_dither_rgb_error {
	int16_t r;
	int16_t g;
	int16_t b;
};

struct display_palette_dither_state {
	enum display_pixel_format current_format;
	uint8_t *converted_buf;
	size_t converted_buf_size;
	/* Error-diffusion row buffers. [0]=current, [1]=next, [2]=row-after-next.
	 * Floyd-Steinberg uses [0]+[1]; Atkinson uses all three. NULL otherwise.
	 */
	struct display_palette_dither_rgb_error *err_rows[3];
	size_t err_row_len;
};

struct display_palette_dither_inst {
	const struct device *dev;
	struct display_palette_dither_state *state;
};

/* Bitmask of non-I_4 formats this helper accepts (driven by Kconfig). */
uint32_t display_palette_dither_supported_formats(void);

/* Internal. Invoked through macros below. */
int _pdither_preprocess(const struct device *dev,
			const struct display_buffer_descriptor **desc_in_out,
			const void **buf_in_out, struct display_buffer_descriptor *scratch);

void _pdither_patch_caps(const struct device *dev, struct display_capabilities *caps);

int _pdither_claim(const struct device *dev, enum display_pixel_format pf);

#define _PDITHER_I4_BYTES(w, h) (DIV_ROUND_UP((w), 2U) * (h))

/*
 * Per-instance error-diffusion row buffers. Both supported error-diffusion
 * algorithms (Floyd-Steinberg, Atkinson) need at least two rows; Atkinson
 * additionally writes to the row-after-next so it needs three. Each row is
 * sized to width+2 because Atkinson diffuses to x+2 - the extra slack is
 * harmless when only Floyd-Steinberg is enabled.
 */
#if defined(CONFIG_DISPLAY_PALETTE_DITHER_ALGORITHM_ATKINSON)
#define _PDITHER_ED_NROWS 3
#elif defined(CONFIG_DISPLAY_PALETTE_DITHER_ALGORITHM_FLOYD_STEINBERG)
#define _PDITHER_ED_NROWS 2
#else
#define _PDITHER_ED_NROWS 0
#endif

#if _PDITHER_ED_NROWS > 0
#define _PDITHER_ED_LEN(w)   ((w) + 2U)
#define _PDITHER_ED_ROW(tag) _pdither_ed_row_##tag

#define _PDITHER_ED_DEFINE(tag, w)                                                                 \
	static struct display_palette_dither_rgb_error _PDITHER_ED_ROW(                            \
		tag)[_PDITHER_ED_NROWS][_PDITHER_ED_LEN(w)]
#define _PDITHER_ED_R0(tag) _PDITHER_ED_ROW(tag)[0]
#define _PDITHER_ED_R1(tag) _PDITHER_ED_ROW(tag)[1]
#if _PDITHER_ED_NROWS >= 3
#define _PDITHER_ED_R2(tag) _PDITHER_ED_ROW(tag)[2]
#else
#define _PDITHER_ED_R2(tag) NULL
#endif

#else /* no error-diffusion algorithm selected */
#define _PDITHER_ED_DEFINE(tag, w)
#define _PDITHER_ED_LEN(w)  0U
#define _PDITHER_ED_R0(tag) NULL
#define _PDITHER_ED_R1(tag) NULL
#define _PDITHER_ED_R2(tag) NULL
#endif

/**
 * @brief Per-instance machinery the driver invokes once inside its @c DT_INST instantiation macro.
 *
 * Allocates a conversion buffer sized for one full I_4 frame, optional Floyd-Steinberg error rows,
 * the state struct, and a registry entry that binds the @c DEVICE_DT_INST_GET(inst) handle to that
 * state. The driver itself does not need to add any field or wiring.
 *
 * Width and height are read from the driver's own DT node via the standard @c width / @c height
 * integer properties.
 *
 * @param drv   Driver name token (used only for symbol disambiguation).
 * @param inst  DT instance index.
 */
#define DISPLAY_PALETTE_DITHER_INST_DEFINE(drv, inst)                                              \
	static uint8_t _pdither_buf_##drv##_##inst[_PDITHER_I4_BYTES(DT_INST_PROP(inst, width),    \
								     DT_INST_PROP(inst, height))]; \
	_PDITHER_ED_DEFINE(drv##_##inst, DT_INST_PROP(inst, width));                               \
	static struct display_palette_dither_state _pdither_state_##drv##_##inst = {               \
		.current_format = _PDITHER_DEFAULT_FMT,                                            \
		.converted_buf = _pdither_buf_##drv##_##inst,                                      \
		.converted_buf_size = sizeof(_pdither_buf_##drv##_##inst),                         \
		.err_rows = {                                                                      \
			_PDITHER_ED_R0(drv##_##inst),                                              \
			_PDITHER_ED_R1(drv##_##inst),                                              \
			_PDITHER_ED_R2(drv##_##inst),                                              \
		},                                                                                 \
		.err_row_len = _PDITHER_ED_LEN(DT_INST_PROP(inst, width)),                         \
	};                                                                                         \
	static const STRUCT_SECTION_ITERABLE(display_palette_dither_inst,                          \
					     _pdither_reg_##drv##_##inst) = {                      \
		.dev = DEVICE_DT_INST_GET(inst),                                                   \
		.state = &_pdither_state_##drv##_##inst,                                           \
	}

/**
 * @brief Tap-in at the top of the driver's @c write() callback.
 *
 * Rewrites the caller's @p desc and @p buf parameters to point at I_4 data, either passthrough
 * (when the current format is already I_4) or the helper's scratch buffer (when conversion was
 * required). On helper errors the macro returns the error code from the enclosing function.
 *
 * Must be invoked once, before any other use of @p desc / @p buf. Declares a function-scope scratch
 * descriptor; place it at the top of the function.
 *
 * @param dev   The @c const @c struct @c device @c * passed to the callback.
 * @param desc  The @c const @c struct @c display_buffer_descriptor @c * parameter.
 * @param buf   The @c const @c void @c * parameter.
 */
#define DISPLAY_PALETTE_DITHER_PREPROCESS(dev, desc, buf)                                          \
	struct display_buffer_descriptor _pdither_scratch;                                         \
	{                                                                                          \
		int _pdither_ret =                                                                 \
			_pdither_preprocess((dev), &(desc), &(buf), &_pdither_scratch);            \
		if (_pdither_ret < 0) {                                                            \
			return _pdither_ret;                                                       \
		}                                                                                  \
	}

/**
 * @brief Tail of the driver's @c get_capabilities() callback.
 *
 * ORs the helper's extra supported formats into @c caps->supported_pixel_formats and reports the
 * current input format selected by the upper layer (or the Kconfig default) as @c
 * caps->current_pixel_format.
 */
#define DISPLAY_PALETTE_DITHER_PATCH_CAPS(dev, caps) _pdither_patch_caps((dev), (caps))

/**
 * @brief Inside @c set_pixel_format(): ask the helper to claim this format.
 *
 * Returns 0 only when the helper has registered state for this device and the format is
 * one it can take ownership of (I_4 as passthrough, or a Kconfig-enabled RGB format
 * for active dithering). Returns @c -ENOTSUP otherwise, including the case where the
 * helper is compiled out, the device has no registry entry, or the format is unsupported.
 *
 * The driver is responsible for handling its own native formats outside the helper.
 */
#define DISPLAY_PALETTE_DITHER_CLAIM(dev, pf) _pdither_claim((dev), (pf))

/**
 * @brief Inside @c set_pixel_format(): pin the helper into passthrough mode.
 *
 * Tells the helper its current format is I_4 so the next @ref DISPLAY_PALETTE_DITHER_PREPROCESS()
 * short-circuits and the buffer is forwarded untouched. Use when the driver is going to handle
 * the format itself (e.g. SDL falling back to its native RGB888 / ARGB8888 dispatch).
 */
#define DISPLAY_PALETTE_DITHER_PASSTHROUGH(dev) ((void)_pdither_claim((dev), PIXEL_FORMAT_I_4))

#else /* !CONFIG_DISPLAY_PALETTE_DITHER */

static inline uint32_t display_palette_dither_supported_formats(void)
{
	return 0U;
}

#define DISPLAY_PALETTE_DITHER_INST_DEFINE(drv, inst)
#define DISPLAY_PALETTE_DITHER_PREPROCESS(dev, desc, buf)                                          \
	do {                                                                                       \
		(void)(dev);                                                                       \
		(void)(desc);                                                                      \
		(void)(buf);                                                                       \
	} while (0)
#define DISPLAY_PALETTE_DITHER_PATCH_CAPS(dev, caps)                                               \
	do {                                                                                       \
		(void)(dev);                                                                       \
		(void)(caps);                                                                      \
	} while (0)
#define DISPLAY_PALETTE_DITHER_CLAIM(dev, pf) ((void)(dev), (void)(pf), -ENOTSUP)
#define DISPLAY_PALETTE_DITHER_PASSTHROUGH(dev) ((void)(dev))

#endif /* CONFIG_DISPLAY_PALETTE_DITHER */

#endif /* ZEPHYR_DRIVERS_DISPLAY_PALETTE_DITHER_H_ */
