/*
 * Copyright (c) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the Intel ADSP microphone privacy driver interface definitions.
 * @ingroup misc_interfaces
 */

#ifndef __ZEPHYR_INCLUDE_MIC_PRIVACY_H__
#define __ZEPHYR_INCLUDE_MIC_PRIVACY_H__

#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/devicetree.h>

/** @brief Microphone privacy policies */
enum mic_privacy_policy {
	MIC_PRIVACY_DISABLED = 0,           /**< Microphone privacy is disabled */
	MIC_PRIVACY_HW_MANAGED = 1,         /**< Microphone privacy is managed by hardware */
	MIC_PRIVACY_FW_MANAGED = 2,         /**< Microphone privacy is managed by firmware */
	MIC_PRIVACY_FORCE_MIC_DISABLED = 3, /**< Microphone input is forced disabled */
};

/**
 * @brief Mask of audio links selected for DMA data zeroing.
 *
 * Has to match the HW register definition
 * (DZLS bit field in DFMICPVCP register).
 */
union mic_privacy_mask {
	uint32_t value; /**< Raw mask value */
	struct {
		uint32_t sndw:7; /**< SoundWire link mask */
		uint32_t dmic:1; /**< DMIC link mask */
	};
};

/** @cond INTERNAL_HIDDEN */

struct intel_adsp_mic_priv_data {
	uint8_t rsvd;
};

struct intel_adsp_mic_priv_cfg {
	uint32_t base;
	uint32_t regblock_size;
};

/** @endcond */

/** @brief Microphone privacy driver API */
struct mic_privacy_api_funcs {
	/** Enable or disable the FW managed interrupt, connecting fn as its handler */
	void (*enable_fw_managed_irq)(bool enable_irq, const void *fn);
	/** Clear the FW managed interrupt */
	void (*clear_fw_managed_irq)();
	/** Enable or disable the DMIC interrupt, connecting fn as its handler */
	void (*enable_dmic_irq)(bool enable_irq, const void *fn);
	/** Get the DMIC interrupt status */
	bool (*get_dmic_irq_status)(void);
	/** Clear the DMIC interrupt status */
	void (*clear_dmic_irq_status)(void);
	/** Get the currently configured microphone privacy policy */
	enum mic_privacy_policy (*get_policy)();
	/** Get the raw value of the privacy policy register */
	uint32_t (*get_privacy_policy_register_raw_value)();
	/** Get the DMA data zeroing wait time */
	uint32_t (*get_dma_data_zeroing_wait_time)();
	/** Get the DMA data zeroing link select mask */
	uint32_t (*get_dma_data_zeroing_link_select)();
	/** Get the DMIC microphone disable status */
	uint32_t (*get_dmic_mic_disable_status)(void);
	/** Get the FW managed microphone disable status */
	uint32_t (*get_fw_managed_mic_disable_status)();
	/** Enable or disable the FW managed mode */
	void (*set_fw_managed_mode)(bool is_fw_managed_enabled);
	/** Set the FW microphone disable status */
	void (*set_fw_mic_disable_status)(bool fw_mic_disable_status);
	/** Get the FW microphone disable status */
	uint32_t (*get_fw_mic_disable_status)();
};

#endif
