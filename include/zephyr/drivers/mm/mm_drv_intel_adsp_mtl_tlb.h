/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2022 Intel Corporation.
 *
 * Author: Marcin Szkudlinski <marcin.szkudlinski@linux.intel.com>
 *
 */

/**
 * @file
 * @brief Header file for the memory context save and restore API of the Intel ADSP MTL TLB driver.
 * @ingroup mm_drv_apis
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MM_MM_DRV_INTEL_ADSP_MTL_TLB_H_
#define ZEPHYR_INCLUDE_DRIVERS_MM_MM_DRV_INTEL_ADSP_MTL_TLB_H_


/**
 * @brief Save the contents of the physical memory banks into a provided storage buffer.
 *
 * The system must be almost stopped. The operation is destructive: it changes the physical
 * to virtual addresses mapping, leaving the system not operational. Power states of the
 * memory banks stay untouched. It is assumed that
 *	- the dcache memory had been invalidated before
 *	- no remapping of addresses below unused_l2_sram_start_marker has been made
 *	  (this is ensured by the driver itself, which rejects such remapping requests)
 *
 * At this point the memory is still up and running, so it is safe to use libraries like
 * memcpy and the procedure can be called in a Zephyr driver model way.
 *
 * @param storage_buffer Buffer where the memory contents are saved.
 */
typedef void (*mm_save_context)(void *storage_buffer);

/**
 * @brief Get the size of the storage buffer needed to perform a context save.
 *
 * @return Required storage buffer size, in bytes.
 */
typedef uint32_t (*mm_get_storage_size)(void);

/**
 * @brief Restore the contents and power state of the physical memory banks.
 *
 * Restores the memory contents from the storage buffer and recreates the physical to
 * virtual mappings. As the system memory is down at this point, the procedure
 *  - MUST be located in the IMR memory region
 *  - MUST be called using a simple extern procedure call, since the API table is not
 *    yet loaded
 *  - MUST NOT use libraries like memcpy; use instead a special version, bmemcpy,
 *    located in IMR
 *
 * @param storage_buffer Buffer holding the previously saved memory contents.
 */
void adsp_mm_restore_context(void *storage_buffer);

/**
 * @brief Get the first unused address in L2 virtual memory.
 *
 * @return First unused, aligned address in L2 virtual memory.
 */
uintptr_t adsp_mm_get_unused_l2_start_aligned(void);

/** @brief Memory context save API of the Intel ADSP MTL TLB driver. */
struct intel_adsp_tlb_api {
	/** @copybrief mm_save_context */
	mm_save_context save_context;
	/** @copybrief mm_get_storage_size */
	mm_get_storage_size get_storage_size;
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_MM_MM_DRV_INTEL_ADSP_MTL_TLB_H_ */
