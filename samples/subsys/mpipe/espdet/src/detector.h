/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DETECTOR_H_
#define DETECTOR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One detected object, in the coordinates of the analyzed frame */
struct detector_box {
	int16_t x0;
	int16_t y0;
	int16_t x1;
	int16_t y1;
	/** Index of the class in the model's label list */
	uint8_t category;
	/** Confidence, in percent */
	uint8_t score;
};

/**
 * @brief Load the embedded model and set up pre- and post-processing.
 *
 * @retval 0 Success
 * @retval -ENOMEM The model could not be loaded
 */
int detector_init(void);

/**
 * @brief Detect objects in an RGB565 frame.
 *
 * The frame is letterboxed and resized to the model input, and the boxes are
 * mapped back to the frame coordinates.
 *
 * @param frame RGB565 pixels, @p width x @p height, no padding between lines
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param big_endian True for RGB565X (big-endian) pixels
 * @param boxes Array receiving the detections, best score first
 * @param max_boxes Capacity of @p boxes
 *
 * @return The number of detections written to @p boxes, or -EINVAL if
 *	   detector_init() did not succeed.
 */
int detector_run(const void *frame, uint16_t width, uint16_t height, bool big_endian,
		 struct detector_box *boxes, size_t max_boxes);

#ifdef __cplusplus
}
#endif

#endif /* DETECTOR_H_ */
