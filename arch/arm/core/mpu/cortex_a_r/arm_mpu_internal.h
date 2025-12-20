/* SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2019 Lexmark International, Inc.
 */

/**
 * @file
 * @brief ARM Cortex-A/R MPU internal helper functions
 *
 * Internal helper functions for ARM Cortex-A and Cortex-R MPU configuration
 * and management. These functions provide low-level access to the MPU
 * coprocessor registers using inline assembly.
 */

#include <zephyr/sys/math_extras.h>

/**
 * @brief Get the number of supported MPU regions
 *
 * Reads the MPU TYPE coprocessor register to determine how many memory
 * protection regions are available in the hardware.
 *
 * @return Number of supported MPU regions
 */
static inline uint8_t get_num_regions(void)
{
	uint32_t type;

	__asm__ volatile("mrc p15, 0, %0, c0, c0, 4" : "=r" (type) ::);

	type = (type & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;

	return (uint8_t)type;
}

/**
 * @brief Get MPU region attributes
 *
 * Reads the region attributes from the MPU coprocessor register.
 *
 * @return Region attributes value
 */
static inline uint32_t get_region_attributes(void)
{
	uint32_t attr;

	__asm__ volatile("mrc p15, 0, %0, c6, c1, 4" : "=r" (attr) ::);
	return attr;
}

/**
 * @brief Get MPU region base address
 *
 * Reads the base address of the currently selected MPU region.
 *
 * @return Region base address
 */
static inline uint32_t get_region_base_address(void)
{
	uint32_t addr;

	__asm__ volatile("mrc p15, 0, %0, c6, c1, 0" : "=r" (addr) ::);
	return addr;
}

/**
 * @brief Get MPU region size configuration
 *
 * Reads the size configuration of the currently selected MPU region.
 *
 * @return Region size configuration value
 */
static inline uint32_t get_region_size(void)
{
	uint32_t size;

	__asm__ volatile("mrc p15, 0, %0, c6, c1, 2" : "=r" (size) ::);
	return size;
}

/**
 * @brief Set MPU region attributes
 *
 * Writes the region attributes to the MPU coprocessor register.
 *
 * @param attr Region attributes value to set
 */
static inline void set_region_attributes(uint32_t attr)
{
	__asm__ volatile("mcr p15, 0, %0, c6, c1, 4" :: "r" (attr) :);
}

/**
 * @brief Set MPU region base address
 *
 * Writes the base address to the MPU coprocessor register for the
 * currently selected region.
 *
 * @param addr Base address to set
 */
static inline void set_region_base_address(uint32_t addr)
{
	__asm__ volatile("mcr p15, 0, %0, c6, c1, 0" :: "r" (addr) :);
}

/**
 * @brief Set the MPU region number for subsequent operations
 *
 * Selects which MPU region will be accessed by subsequent reads or writes
 * to the MPU coprocessor registers.
 *
 * @param index MPU region index to select
 */
static inline void set_region_number(uint32_t index)
{
	__asm__ volatile("mcr p15, 0, %0, c6, c2, 0" :: "r" (index) :);
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
	set_region_number(index);

	return get_region_base_address() & MPU_RBAR_ADDR_Msk;
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
 * @brief Set MPU region size configuration
 *
 * Writes the size configuration to the MPU coprocessor register for the
 * currently selected region.
 *
 * @param size Size configuration value to set
 */
static inline void set_region_size(uint32_t size)
{
	__asm__ volatile("mcr p15, 0, %0, c6, c1, 2" :: "r" (size) :);
}

/**
 * @brief Clear (disable) an MPU region
 *
 * Disables the specified MPU region by clearing its enable bit.
 *
 * @param rnr MPU region number to clear
 */
static inline void ARM_MPU_ClrRegion(uint32_t rnr)
{
	set_region_number(rnr);
	/* clear size field, which contains enable bit */
	set_region_size(0);
}

/**
 * This internal function checks if region is enabled or not.
 *
 * Note:
 *   The caller must provide a valid region number.
 */
static inline int is_enabled_region(uint32_t index)
{
	set_region_number(index);

	return (get_region_size() & MPU_RASR_ENABLE_Msk) ? 1 : 0;
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
	set_region_number(r_index);

	return (get_region_attributes() & MPU_RASR_AP_Msk) >> MPU_RASR_AP_Pos;
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

	set_region_number(r_index);

	r_addr_start = get_region_base_address() & MPU_RBAR_ADDR_Msk;

	r_size_lshift = ((get_region_size() & MPU_RASR_SIZE_Msk) >>
		MPU_RASR_SIZE_Pos) + 1;

	r_addr_end = r_addr_start + (1UL << r_size_lshift) - 1;

	size = size == 0 ? 0 : size - 1;
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
	set_region_number(index);

	uint32_t rasr_size =
		(get_region_size() & MPU_RASR_SIZE_Msk) >> MPU_RASR_SIZE_Pos;

	return mpu_rasr_size_to_size(rasr_size);
}
