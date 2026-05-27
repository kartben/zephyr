/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Color dithering helpers for display drivers.
 * @ingroup display_color_dither
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_COLOR_DITHER_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_COLOR_DITHER_H_

/**
 * @defgroup display_color_dither Color dithering
 * @since 4.5
 * @version 0.1.0
 * @in_driverbackendgroup{display_interface}
 *
 * @brief RGB-to-palette dithering helpers for display drivers.
 *
 * These helpers let a driver whose native write path consumes @ref PIXEL_FORMAT_I_4 also accept
 * configured RGB input formats. The helper converts each RGB write buffer to packed
 * @ref PIXEL_FORMAT_I_4 using the driver's color palette before the native write code runs.
 *
 * A driver uses the helper at four points:
 *
 * - Embed per-instance state in the driver's data with @ref DISPLAY_COLOR_DITHER_DATA.
 * - Define and initialize backing storage with @ref DISPLAY_COLOR_DITHER_DEFINE and
 *   @ref DISPLAY_COLOR_DITHER_INIT.
 * - Call @ref display_color_dither_prepare at the start of the display_driver_api.write()
 *   operation.
 * - Call @ref display_color_dither_patch_caps after filling display capabilities in the driver's
 *   display_driver_api.get_capabilities() operation.
 * - Call @ref display_color_dither_claim, or @ref display_color_dither_passthrough for native
 *   formats, from the display_driver_api.set_pixel_format() operation.
 *
 * Typical integration:
 *
 * @code{.c}
 *   #include <zephyr/drivers/display/color_dither.h>
 *
 *   struct foo_data {
 *           DISPLAY_COLOR_DITHER_DATA;
 *           // ...driver state...
 *   };
 *
 *   static int foo_write(const struct device *dev,
 *                        const uint16_t x, const uint16_t y,
 *                        const struct display_buffer_descriptor *desc,
 *                        const void *buf)
 *   {
 *           struct display_buffer_descriptor scratch;
 *           struct foo_data *data = dev->data;
 *           int ret = display_color_dither_prepare(dev, DISPLAY_COLOR_DITHER_STATE(data),
 *                                                  &desc, &buf, &scratch);
 *
 *           if (ret < 0) {
 *                   return ret;
 *           }
 *           // ...existing I_4-only body, unchanged...
 *   }
 *
 *   static void foo_get_capabilities(const struct device *dev, struct display_capabilities *caps)
 *   {
 *           struct foo_data *data = dev->data;
 *
 *           // ...existing population of caps (advertising I_4 only)...
 *           display_color_dither_patch_caps(DISPLAY_COLOR_DITHER_STATE(data), caps);
 *   }
 *
 *   static int foo_set_pixel_format(const struct device *dev,
 *                                   enum display_pixel_format pf)
 *   {
 *           struct foo_data *data = dev->data;
 *
 *           if (pf == PIXEL_FORMAT_I_4) {
 *                   display_color_dither_passthrough(DISPLAY_COLOR_DITHER_STATE(data));
 *                   return 0;
 *           }
 *           return display_color_dither_claim(DISPLAY_COLOR_DITHER_STATE(data), pf);
 *   }
 *
 *   // In the driver instance definition, define backing storage before data:
 *   #define FOO_DEFINE(inst)                                                         \
 *           DISPLAY_COLOR_DITHER_DEFINE(inst);                                       \
 *           static struct foo_data foo_data_##inst = {                               \
 *                   IF_ENABLED(CONFIG_DISPLAY_COLOR_DITHER,                          \
 *                              (.color_dither = DISPLAY_COLOR_DITHER_INIT(inst),))   \
 *           };                                                                       \
 *           DEVICE_DT_INST_DEFINE(inst, ...);
 * @endcode
 *
 * When @kconfig{CONFIG_DISPLAY_COLOR_DITHER} is disabled, the backing-storage macro is empty,
 * claim returns @c -ENOTSUP, and the other inline helpers leave driver behavior unchanged.
 *
 * @{
 */

#include <zephyr/drivers/display.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

struct display_color_dither_state;

#if defined(CONFIG_DISPLAY_COLOR_DITHER) || defined(__DOXYGEN__)

/**
 * @brief Embed color dithering state in a driver's runtime data.
 *
 * Use as a member declaration inside the driver's data structure.
 */
#define DISPLAY_COLOR_DITHER_DATA struct display_color_dither_state color_dither

/**
 * @brief Get the color dithering state pointer from driver data.
 *
 * Pass this to the helper functions. When color dithering is disabled, the macro expands to
 * NULL and consumes @p data so driver code does not need extra conditionals.
 *
 * @param data Pointer to driver data containing @ref DISPLAY_COLOR_DITHER_DATA.
 */
#define DISPLAY_COLOR_DITHER_STATE(data) (&(data)->color_dither)

/** @cond INTERNAL_HIDDEN */

/**
 * @brief Per-component signed quantization error accumulated during error-diffusion dithering.
 *
 * One element per pixel column per error-diffusion row buffer.
 */
struct display_color_dither_rgb_error {
	int16_t r; /**< Red channel error */
	int16_t g; /**< Green channel error */
	int16_t b; /**< Blue channel error */
};

/**
 * @brief Runtime state owned by the color dithering helper for one display
 *        device instance.
 *
 * Drivers embed this structure with @ref DISPLAY_COLOR_DITHER_DATA and pass it to the helper
 * functions. Driver code does not need to access any field directly.
 */
struct display_color_dither_state {
	/** Pixel format currently claimed by the helper (or I_4 for pass-through). */
	enum display_pixel_format current_format;
	/** Scratch buffer for the converted I_4 frame. */
	uint8_t *converted_buf;
	/** Size of @c converted_buf in bytes. */
	size_t converted_buf_size;
	/**
	 * Error-diffusion row buffers.
	 *
	 * Index 0 = current row, 1 = next, 2 = row-after-next.
	 * Floyd-Steinberg uses [0] and [1]; Atkinson uses all three. NULL when no error-diffusion
	 * algorithm is active.
	 */
	struct display_color_dither_rgb_error *err_rows[3];
	/** Number of elements in each error-diffusion row buffer. */
	size_t err_row_len;
};

#if defined(CONFIG_DISPLAY_COLOR_DITHER_DEFAULT_RGB888)
#define COLOR_DITHER_DEFAULT_FMT PIXEL_FORMAT_RGB_888
#elif defined(CONFIG_DISPLAY_COLOR_DITHER_DEFAULT_RGB565)
#define COLOR_DITHER_DEFAULT_FMT PIXEL_FORMAT_RGB_565
#else
#define COLOR_DITHER_DEFAULT_FMT PIXEL_FORMAT_I_4
#endif

#define COLOR_DITHER_I4_BYTES(w, h) (DIV_ROUND_UP((w), 2U) * (h))

/*
 * Per-instance row buffers for the error-diffusion algorithms only.
 */
#if defined(CONFIG_DISPLAY_COLOR_DITHER_ALGORITHM_ATKINSON)
#define COLOR_DITHER_ERRDIFF_NROWS 3
#elif defined(CONFIG_DISPLAY_COLOR_DITHER_ALGORITHM_FLOYD_STEINBERG)
#define COLOR_DITHER_ERRDIFF_NROWS 2
#else
#define COLOR_DITHER_ERRDIFF_NROWS 0
#endif

#if COLOR_DITHER_ERRDIFF_NROWS > 0
#define COLOR_DITHER_ERRDIFF_LEN(w)   ((w) + 2U)
#define COLOR_DITHER_ERRDIFF_ROW(tag) color_dither_errdiff_row_##tag

#define COLOR_DITHER_ERRDIFF_DEFINE(tag, w)                                                        \
	static struct display_color_dither_rgb_error COLOR_DITHER_ERRDIFF_ROW(                     \
		tag)[COLOR_DITHER_ERRDIFF_NROWS][COLOR_DITHER_ERRDIFF_LEN(w)]
#define COLOR_DITHER_ERRDIFF_R0(tag) COLOR_DITHER_ERRDIFF_ROW(tag)[0]
#define COLOR_DITHER_ERRDIFF_R1(tag) COLOR_DITHER_ERRDIFF_ROW(tag)[1]
#if COLOR_DITHER_ERRDIFF_NROWS >= 3
#define COLOR_DITHER_ERRDIFF_R2(tag) COLOR_DITHER_ERRDIFF_ROW(tag)[2]
#else
#define COLOR_DITHER_ERRDIFF_R2(tag) NULL
#endif

#else /* no error-diffusion algorithm selected */
#define COLOR_DITHER_ERRDIFF_DEFINE(tag, w)
#define COLOR_DITHER_ERRDIFF_LEN(w)  0U
#define COLOR_DITHER_ERRDIFF_R0(tag) NULL
#define COLOR_DITHER_ERRDIFF_R1(tag) NULL
#define COLOR_DITHER_ERRDIFF_R2(tag) NULL
#endif

/** @endcond */

/**
 * @brief Define color dithering backing storage for one display driver instance.
 *
 * Use once for each driver instance that can use the helper. This allocates the conversion buffer
 * and optional error-diffusion rows.
 * Buffer dimensions come from the instance's @c width and @c height devicetree properties.
 *
 * @kconfig_dep{CONFIG_DISPLAY_COLOR_DITHER}
 *
 * @param inst  DT instance index.
 */
#define DISPLAY_COLOR_DITHER_DEFINE(inst)                                                          \
	static uint8_t color_dither_buf_##inst[COLOR_DITHER_I4_BYTES(DT_INST_PROP(inst, width),    \
								     DT_INST_PROP(inst, height))]; \
	COLOR_DITHER_ERRDIFF_DEFINE(inst, DT_INST_PROP(inst, width))

/**
 * @brief Initialize @ref DISPLAY_COLOR_DITHER_DATA for one display driver instance.
 *
 * Use in the driver data initializer after @ref DISPLAY_COLOR_DITHER_DEFINE has defined the
 * backing storage for the same instance.
 *
 * @kconfig_dep{CONFIG_DISPLAY_COLOR_DITHER}
 *
 * @param inst DT instance index.
 */
#define DISPLAY_COLOR_DITHER_INIT(inst)                                                            \
	{                                                                                          \
		.current_format = COLOR_DITHER_DEFAULT_FMT,                                        \
		.converted_buf = color_dither_buf_##inst,                                          \
		.converted_buf_size = sizeof(color_dither_buf_##inst),                             \
		.err_rows =                                                                        \
			{                                                                          \
				COLOR_DITHER_ERRDIFF_R0(inst),                                     \
				COLOR_DITHER_ERRDIFF_R1(inst),                                     \
				COLOR_DITHER_ERRDIFF_R2(inst),                                     \
			},                                                                         \
		.err_row_len = COLOR_DITHER_ERRDIFF_LEN(DT_INST_PROP(inst, width)),                \
	}

/**
 * @brief Prepare a write buffer for an @ref PIXEL_FORMAT_I_4 native path.
 *
 * Call at the start of the driver's display_driver_api.write() operation.
 * If the helper owns the current pixel format, it converts @p buf to packed @ref PIXEL_FORMAT_I_4
 * and updates @p desc and @p buf to point at the converted data.
 * In pass-through mode, or when the state has no conversion buffer, the inputs are left unchanged.
 *
 * @kconfig_dep{CONFIG_DISPLAY_COLOR_DITHER}
 *
 * @param dev     Display device. Used to read the driver's color palette.
 * @param state   Color dithering state embedded in the driver data.
 * @param desc    Address of the write callback's descriptor pointer.
 * @param buf     Address of the write callback's buffer pointer.
 * @param scratch Scratch descriptor storage owned by the caller.
 *
 * @retval 0 The write can continue.
 * @retval -EINVAL The input descriptor is invalid for the selected format.
 * @retval -ENOMEM The conversion buffer is too small.
 * @retval -ENOTSUP The selected format or palette cannot be converted.
 */
int display_color_dither_prepare(const struct device *dev, struct display_color_dither_state *state,
				 const struct display_buffer_descriptor **desc, const void **buf,
				 struct display_buffer_descriptor *scratch);

/**
 * @brief Add helper-owned RGB formats to display capabilities.
 *
 * Call at the end of the driver's display_driver_api.get_capabilities() operation, after the driver
 * has filled its native capabilities and color palette.
 *
 * This adds the RGB formats enabled by Kconfig to the list of supported pixel formats in
 * display_capabilities::supported_pixel_formats.
 *
 * When the helper is actively converting, it also reports the helper-owned input format in
 * display_capabilities::current_pixel_format.
 *
 * @kconfig_dep{CONFIG_DISPLAY_COLOR_DITHER}
 *
 * @param state Color dithering state embedded in the driver data.
 * @param caps Capabilities to patch.
 */
void display_color_dither_patch_caps(struct display_color_dither_state *state,
				     struct display_capabilities *caps);

/**
 * @brief Select a pixel format for helper-managed conversion.
 *
 * Call from the driver's display_driver_api.set_pixel_format() operation for formats the native
 * driver does not handle directly.
 *
 * The helper accepts @ref PIXEL_FORMAT_I_4 as pass-through and configured RGB formats as conversion
 * inputs. Native driver formats should be handled by the driver instead.
 *
 * @kconfig_dep{CONFIG_DISPLAY_COLOR_DITHER}
 *
 * @param state Color dithering state embedded in the driver data.
 * @param pf    Pixel format requested by the caller.
 *
 * @retval 0 Success.
 * @retval -ENOTSUP The helper is disabled, @p state has no conversion buffer, or @p pf is
 *                  unsupported.
 */
int display_color_dither_claim(struct display_color_dither_state *state,
			       enum display_pixel_format pf);

/**
 * @brief Keep the helper out of the driver's native write path.
 *
 * Call from the driver's display_driver_api.set_pixel_format() operation, after selecting a format
 * the driver will handle itself.
 * The next @ref display_color_dither_prepare call will leave the write descriptor and buffer
 * pointer unchanged.
 *
 * @kconfig_dep{CONFIG_DISPLAY_COLOR_DITHER}
 *
 * @param state Color dithering state embedded in the driver data.
 */
void display_color_dither_passthrough(struct display_color_dither_state *state);

#else /* !CONFIG_DISPLAY_COLOR_DITHER */

/** @cond INTERNAL_HIDDEN */

#define DISPLAY_COLOR_DITHER_DATA
#define DISPLAY_COLOR_DITHER_STATE(data) ((void)(data), NULL)
#define DISPLAY_COLOR_DITHER_DEFINE(inst)
#define DISPLAY_COLOR_DITHER_INIT(inst) {0}

static inline int display_color_dither_prepare(const struct device *dev,
					       struct display_color_dither_state *state,
					       const struct display_buffer_descriptor **desc,
					       const void **buf,
					       struct display_buffer_descriptor *scratch)
{
	(void)dev;
	(void)state;
	(void)desc;
	(void)buf;
	(void)scratch;

	return 0;
}

static inline void display_color_dither_patch_caps(struct display_color_dither_state *state,
						   struct display_capabilities *caps)
{
	(void)state;
	(void)caps;
}

static inline int display_color_dither_claim(struct display_color_dither_state *state,
					     enum display_pixel_format pf)
{
	(void)state;
	(void)pf;

	return -ENOTSUP;
}

static inline void display_color_dither_passthrough(struct display_color_dither_state *state)
{
	(void)state;
}

/** @endcond */

#endif /* CONFIG_DISPLAY_COLOR_DITHER */

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_COLOR_DITHER_H_ */
