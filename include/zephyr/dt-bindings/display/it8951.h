/*
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree constants for the IT8951 e-paper timing controller
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_

/**
 * @defgroup it8951_update_modes IT8951 waveform update modes
 * @brief Waveform modes used when triggering a display refresh on the IT8951.
 *
 * These values are passed to the IT8951 DISPLAY_AREA command and select the
 * waveform used to drive the e-paper panel during a refresh.  Higher modes
 * provide better image quality at the cost of longer refresh time.
 *
 * @{
 */

/** INIT – full-screen clear, longest refresh, removes ghosting */
#define IT8951_DT_UPDATE_MODE_INIT   0
/** DU   – direct update, monochrome, fastest */
#define IT8951_DT_UPDATE_MODE_DU     1
/** GC16 – 16-level grayscale, high quality (default) */
#define IT8951_DT_UPDATE_MODE_GC16   2
/** GL16 – 16-level grayscale with ghost compensation */
#define IT8951_DT_UPDATE_MODE_GL16   3
/** GLR16 – GL16 with extra reduction pass */
#define IT8951_DT_UPDATE_MODE_GLR16  4

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_ */
