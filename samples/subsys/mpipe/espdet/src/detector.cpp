/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* ESPDet-Pico inference with ESP-DL */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_detect_espdet_postprocessor.hpp"

#include "detector.h"

LOG_MODULE_REGISTER(detector, CONFIG_LOG_DEFAULT_LEVEL);

/* ESP-DL reads the parameters in place, 16-byte aligned, then copies them to PSRAM */
static const uint8_t espdet_model[] __aligned(16) = {
#include "espdet_model.inc"
};

static dl::Model *model;
static dl::image::ImagePreprocessor *preprocessor;
static dl::detect::ESPDetPostProcessor *postprocessor;

/* The three output stages of ESPDet-Pico, of stride 8, 16 and 32 */
static const std::vector<dl::detect::anchor_point_stage_t> stages = {
	{8, 8, 4, 4},
	{16, 16, 8, 8},
	{32, 32, 16, 16},
};

extern "C" int detector_init(void)
{
	const float score_thr = CONFIG_ESPDET_SCORE_THRESHOLD / 100.0f;
	const float nms_thr = CONFIG_ESPDET_NMS_THRESHOLD / 100.0f;

	model = new dl::Model(reinterpret_cast<const char *>(espdet_model),
			      fbs::MODEL_LOCATION_IN_FLASH_RODATA, CONFIG_ESPDET_MAX_INTERNAL_SIZE);
	if (model == nullptr || model->get_inputs().empty()) {
		LOG_ERR("Cannot load the model");
		return -ENOMEM;
	}

	model->minimize();

	/* Pixels normalized to [0, 1], letterboxed with the gray of the training */
	preprocessor = new dl::image::ImagePreprocessor(model, {0, 0, 0}, {255, 255, 255});
	preprocessor->enable_letterbox({114, 114, 114});
	postprocessor = new dl::detect::ESPDetPostProcessor(model, preprocessor, score_thr, nms_thr,
							    CONFIG_ESPDET_TOP_K, stages);

	auto *input = preprocessor->get_model_input();

	LOG_INF("Model loaded, input %dx%d", input->shape[2], input->shape[1]);

	return 0;
}

extern "C" int detector_run(const void *frame, uint16_t width, uint16_t height, bool big_endian,
			    struct detector_box *boxes, size_t max_boxes)
{
	dl::image::img_t img = {
		.data = const_cast<void *>(frame),
		.width = width,
		.height = height,
		.pix_type = big_endian ? dl::image::DL_IMAGE_PIX_TYPE_RGB565BE
				       : dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
	};
	size_t count = 0;

	if (postprocessor == nullptr) {
		return -EINVAL;
	}

	preprocessor->preprocess(img);
	model->run();
	postprocessor->clear_result();
	postprocessor->postprocess();

	for (const auto &res : postprocessor->get_result(width, height)) {
		if (count == max_boxes) {
			break;
		}

		boxes[count].x0 = res.box[0];
		boxes[count].y0 = res.box[1];
		boxes[count].x1 = res.box[2];
		boxes[count].y1 = res.box[3];
		boxes[count].category = res.category;
		boxes[count].score = (uint8_t)(res.score * 100.0f);
		count++;
	}

	return count;
}
