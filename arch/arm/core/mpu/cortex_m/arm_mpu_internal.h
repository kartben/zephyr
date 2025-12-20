/* SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2019 Lexmark International, Inc.
 */

/**
 * @file
 * @brief ARM Cortex-M MPU internal helper functions
 *
 * Internal helper functions for ARM Cortex-M MPU configuration and management.
 * These functions provide low-level access to the MPU hardware registers.
 */

#include <zephyr/sys/math_extras.h>

/**
 * @brief Get the number of supported MPU regions
 *
 * Reads the MPU TYPE register to determine how many memory protection
 * regions are available in the hardware.
 *
 * @return Number of supported MPU regions
 */
static inline uint8_t get_num_regions(void)
{
	uint32_t type = MPU->TYPE;

	type = (type & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;

	return (uint8_t)type;
}

/**
 * @brief Set the MPU region number for subsequent operations
 *
 * Selects which MPU region will be accessed by subsequent reads or writes
 * to the MPU configuration registers.
 *
 * @param index MPU region index to select
 */
static inline void set_region_number(uint32_t index)
{
	MPU->RNR = index;
}

/**
 * @brief Get the base address of an MPU region
 *
 * Retrieves the base address of the specified MPU region.
 *
 * @param index MPU region index
 * @return Base address of the region
 */
static inline uint32_t mpu_region_get_base(uint32_t index)
{
	MPU->RNR = index;
	return MPU->RBAR & MPU_RBAR_ADDR_Msk;
}

/**
 * @brief Convert MPU RASR SIZE field to actual size in bytes
 *
 * This internal function converts the SIZE field value of MPU_RASR
 * to the region size (in bytes).
 *
 * @param rasr_size SIZE field value from MPU_RASR register
 * @return Region size in bytes
 */
static inline uint32_t mpu_rasr_size_to_size(uint32_t rasr_size)
{
	return 1 << (rasr_size + 1U);
}

/**
 * This internal function checks if region is enabled or not.
 *
 * Note:
 *   The caller must provide a valid region number.
 */
static inline int is_enabled_region(uint32_t index)
{
	/* Lock IRQs to ensure RNR value is correct when reading RASR. */
	unsigned int key;
	uint32_t rasr;

	key = irq_lock();
	MPU->RNR = index;
	rasr = MPU->RASR;
	irq_unlock(key);

	return (rasr & MPU_RASR_ENABLE_Msk) ? 1 : 0;
}

/**
 * This internal function returns the access permissions of an MPU region
 * specified by its region index.
 *
 * Note:
 *   The caller must provide a valid region number.
 */
static inline uint32_t get_region_ap(uint32_t r_index)
{
	/* Lock IRQs to ensure RNR value is correct when reading RASR. */
	unsigned int key;
	uint32_t rasr;

	key = irq_lock();
	MPU->RNR = r_index;
	rasr = MPU->RASR;
	irq_unlock(key);

	return (rasr & MPU_RASR_AP_Msk) >> MPU_RASR_AP_Pos;
}

/**
 * This internal function checks if the given buffer is in the region.
 *
 * Note:
 *   The caller must provide a valid region number.
 */
static inline int is_in_region(uint32_t r_index, uint32_t start, uint32_t size)
{
	uint32_t r_addr_start;
	uint32_t r_size_lshift;
	uint32_t r_addr_end;
	uint32_t end;

	/* Lock IRQs to ensure RNR value is correct when reading RBAR, RASR. */
	unsigned int key;
	uint32_t rbar, rasr;

	key = irq_lock();
	MPU->RNR = r_index;
	rbar = MPU->RBAR;
	rasr = MPU->RASR;
	irq_unlock(key);

	r_addr_start = rbar & MPU_RBAR_ADDR_Msk;
	r_size_lshift = ((rasr & MPU_RASR_SIZE_Msk) >>
			MPU_RASR_SIZE_Pos) + 1U;
	r_addr_end = r_addr_start + (1UL << r_size_lshift) - 1UL;

	size = size == 0U ? 0U : size - 1U;
	if (u32_add_overflow(start, size, &end)) {
		return 0;
	}

	if ((start >= r_addr_start) && (end <= r_addr_end)) {
		return 1;
	}

	return 0;
}

static inline uint32_t mpu_region_get_size(uint32_t index)
{
	MPU->RNR = index;
	uint32_t rasr_size =
		(MPU->RASR & MPU_RASR_SIZE_Msk) >> MPU_RASR_SIZE_Pos;

	return mpu_rasr_size_to_size(rasr_size);
}
