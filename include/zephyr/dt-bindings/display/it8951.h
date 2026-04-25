/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_

/**
 * @brief IT8951 display controller device tree bindings
 * @defgroup it8951_dt_bindings IT8951 DT bindings
 * @ingroup display_interface
 * @{
 */

/**
 * @name IT8951 waveform modes for the waveform-mode property.
 *
 * These modes control the display update algorithm used when refreshing the
 * e-paper panel.  Higher-quality modes (e.g. GC16) produce less ghosting but
 * take longer to complete.
 * @{
 */

/** Full initialisation — clears the panel to white before applying image. */
#define IT8951_WAVEFORM_INIT  0
/** Direct update — fast 2-level (black/white) update. */
#define IT8951_WAVEFORM_DU    1
/** Grayscale clear 16 — full 16-level grayscale, best image quality. */
#define IT8951_WAVEFORM_GC16  2
/** Grayscale low — 16-level grayscale, reduced refresh time. */
#define IT8951_WAVEFORM_GL16  3
/** Ghost-free GC16 — GC16 with ghost-removal pre-pass. */
#define IT8951_WAVEFORM_GLR16 4
/** Ghost-reduction GC16 — GC16 with ghost-reduction optimisation. */
#define IT8951_WAVEFORM_GLD16 5
/** Animation mode — very fast 2-level update for sequential frames. */
#define IT8951_WAVEFORM_A2    6

/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_DISPLAY_IT8951_H_ */
