/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_

/*
 * IT8951 display update waveform modes.
 *
 * The controller firmware maps these numeric values to panel-specific LUTs.
 */
#define IT8951_DISPLAY_MODE_INIT 0
#define IT8951_DISPLAY_MODE_DU   1
#define IT8951_DISPLAY_MODE_GC16 2
#define IT8951_DISPLAY_MODE_GL16 3
#define IT8951_DISPLAY_MODE_A2   4

/* IT8951 load-image pixel formats. */
#define IT8951_PIXEL_FORMAT_2BPP 0
#define IT8951_PIXEL_FORMAT_3BPP 1
#define IT8951_PIXEL_FORMAT_4BPP 2
#define IT8951_PIXEL_FORMAT_8BPP 3

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_ */
