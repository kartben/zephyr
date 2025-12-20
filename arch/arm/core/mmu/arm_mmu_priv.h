/*
 * ARMv7 MMU support
 *
 * Private data declarations
 *
 * Copyright (c) 2021 Weidmueller Interface GmbH & Co. KG
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM MMU private definitions
 *
 * Private data structures and definitions for ARMv7-A/R Memory Management
 * Unit (MMU) support. This file contains page table entry formats, MMU
 * control register bit definitions, and internal structures used by the
 * MMU implementation.
 *
 * Reference: ARM Architecture Reference Manual, ARMv7-A and ARMv7-R edition
 * ARM document ID DDI0406C Rev. d, March 2018
 */

#ifndef ZEPHYR_ARCH_AARCH32_ARM_MMU_PRIV_H_
#define ZEPHYR_ARCH_AARCH32_ARM_MMU_PRIV_H_

/*
 * Comp.:
 * ARM Architecture Reference Manual, ARMv7-A and ARMv7-R edition
 * ARM document ID DDI0406C Rev. d, March 2018
 * L1 / L2 page table entry formats and entry type IDs:
 * Chapter B3.5.1, fig. B3-4 and B3-5, p. B3-1323 f.
 */

/** @brief Number of entries in Level 1 page table */
#define ARM_MMU_PT_L1_NUM_ENTRIES		4096
/** @brief Number of entries in Level 2 page table */
#define ARM_MMU_PT_L2_NUM_ENTRIES		256

/** @brief Shift amount for L1 page table index from physical address */
#define ARM_MMU_PTE_L1_INDEX_PA_SHIFT		20
/** @brief Mask for L1 page table index */
#define ARM_MMU_PTE_L1_INDEX_MASK		0xFFF
/** @brief Shift amount for L2 page table index from physical address */
#define ARM_MMU_PTE_L2_INDEX_PA_SHIFT		12
/** @brief Mask for L2 page table index */
#define ARM_MMU_PTE_L2_INDEX_MASK		0xFF
/** @brief Shift amount for L2 page table address */
#define ARM_MMU_PT_L2_ADDR_SHIFT		10
/** @brief Mask for L2 page table address */
#define ARM_MMU_PT_L2_ADDR_MASK			0x3FFFFF
/** @brief Shift amount for small page address in L2 PTE */
#define ARM_MMU_PTE_L2_SMALL_PAGE_ADDR_SHIFT	12
/** @brief Mask for small page address in L2 PTE */
#define ARM_MMU_PTE_L2_SMALL_PAGE_ADDR_MASK	0xFFFFF
/** @brief Mask for address bits below page granularity */
#define ARM_MMU_ADDR_BELOW_PAGE_GRAN_MASK	0xFFF

/** @brief Page table entry ID: Invalid/fault entry */
#define ARM_MMU_PTE_ID_INVALID			0x0
/** @brief Page table entry ID: Points to Level 2 page table */
#define ARM_MMU_PTE_ID_L2_PT			0x1
/** @brief Page table entry ID: Section (1MB) mapping */
#define ARM_MMU_PTE_ID_SECTION			0x2
/** @brief Page table entry ID: Large page (64KB) */
#define ARM_MMU_PTE_ID_LARGE_PAGE		0x1
/** @brief Page table entry ID: Small page (4KB) */
#define ARM_MMU_PTE_ID_SMALL_PAGE		0x2

/** @brief Access Permission: Disable write access */
#define ARM_MMU_PERMS_AP2_DISABLE_WR		0x2
/** @brief Access Permission: Enable unprivileged (PL0) access */
#define ARM_MMU_PERMS_AP1_ENABLE_PL0		0x1
/** @brief TEX[2]: Indicates cacheable memory */
#define ARM_MMU_TEX2_CACHEABLE_MEMORY		0x4

/** @brief TEX cache attributes: Write-Back, Write-Allocate */
#define ARM_MMU_TEX_CACHE_ATTRS_WB_WA		0x1
/** @brief TEX cache attributes: Write-Through, no Write-Allocate */
#define ARM_MMU_TEX_CACHE_ATTRS_WT_nWA		0x2
/** @brief TEX cache attributes: Write-Back, no Write-Allocate */
#define ARM_MMU_TEX_CACHE_ATTRS_WB_nWA		0x3
/** @brief C bit for Write-Back, Write-Allocate */
#define ARM_MMU_C_CACHE_ATTRS_WB_WA		0
/** @brief B bit for Write-Back, Write-Allocate */
#define ARM_MMU_B_CACHE_ATTRS_WB_WA		1
/** @brief C bit for Write-Through, no Write-Allocate */
#define ARM_MMU_C_CACHE_ATTRS_WT_nWA		1
/** @brief B bit for Write-Through, no Write-Allocate */
#define ARM_MMU_B_CACHE_ATTRS_WT_nWA		0
/** @brief C bit for Write-Back, no Write-Allocate */
#define ARM_MMU_C_CACHE_ATTRS_WB_nWA		1
/** @brief B bit for Write-Back, no Write-Allocate */
#define ARM_MMU_B_CACHE_ATTRS_WB_nWA		1

/*
 * The following defines might vary if support for CPUs without
 * the multiprocessor extensions was to be implemented:
 */

/** @brief TTBR IRGN0 bit (multiprocessor extension only) */
#define ARM_MMU_TTBR_IRGN0_BIT_MP_EXT_ONLY	BIT(6)
/** @brief TTBR Not Outer Shareable bit */
#define ARM_MMU_TTBR_NOS_BIT			BIT(5)
/** @brief Outer region: Non-cacheable */
#define ARM_MMU_TTBR_RGN_OUTER_NON_CACHEABLE	0x0
/** @brief Outer region: Write-Back, Write-Allocate cacheable */
#define ARM_MMU_TTBR_RGN_OUTER_WB_WA_CACHEABLE	0x1
/** @brief Outer region: Write-Through cacheable */
#define ARM_MMU_TTBR_RGN_OUTER_WT_CACHEABLE	0x2
/** @brief Outer region: Write-Back, no Write-Allocate cacheable */
#define ARM_MMU_TTBR_RGN_OUTER_WB_nWA_CACHEABLE	0x3
/** @brief TTBR region configuration shift */
#define ARM_MMU_TTBR_RGN_SHIFT			3
/** @brief TTBR shareable bit */
#define ARM_MMU_TTBR_SHAREABLE_BIT		BIT(1)
/** @brief TTBR IRGN1 bit (multiprocessor extension only) */
#define ARM_MMU_TTBR_IRGN1_BIT_MP_EXT_ONLY	BIT(0)
/** @brief TTBR cacheable bit (non-multiprocessor only) */
#define ARM_MMU_TTBR_CACHEABLE_BIT_NON_MP_ONLY	BIT(0)

/* <-- end MP-/non-MP-specific */

/** @brief Memory domain for OS kernel */
#define ARM_MMU_DOMAIN_OS			0
/** @brief Memory domain for device memory */
#define ARM_MMU_DOMAIN_DEVICE			1
/** @brief DACR value: All domains configured as client */
#define ARM_MMU_DACR_ALL_DOMAINS_CLIENT		0x55555555

/** @brief SCTLR Access Flag Enable bit */
#define ARM_MMU_SCTLR_AFE_BIT			BIT(29)
/** @brief SCTLR TEX remap enable bit */
#define ARM_MMU_SCTLR_TEX_REMAP_ENABLE_BIT	BIT(28)
/** @brief SCTLR Hardware Access flag enable bit */
#define ARM_MMU_SCTLR_HA_BIT			BIT(17)
/** @brief SCTLR Instruction cache enable bit */
#define ARM_MMU_SCTLR_ICACHE_ENABLE_BIT		BIT(12)
/** @brief SCTLR Data cache enable bit */
#define ARM_MMU_SCTLR_DCACHE_ENABLE_BIT		BIT(2)
/** @brief SCTLR Alignment check enable bit */
#define ARM_MMU_SCTLR_CHK_ALIGN_ENABLE_BIT	BIT(1)
/** @brief SCTLR MMU enable bit */
#define ARM_MMU_SCTLR_MMU_ENABLE_BIT		BIT(0)

/** @brief Calculate L2 page table index from pointer */
#define ARM_MMU_L2_PT_INDEX(pt) ((uint32_t)pt - (uint32_t)l2_page_tables) /\
				sizeof(struct arm_mmu_l2_page_table);

/**
 * @brief Level 1 page table entry
 *
 * Union representing a Level 1 (L1) page table entry with different
 * interpretation options for section mappings and page table pointers.
 */
union arm_mmu_l1_page_table_entry {
	/** @brief 1MB section mapping */
	struct {
		uint32_t id			: 2;  /**< Entry type ID [1:0] */
		uint32_t bufferable		: 1;  /**< Bufferable memory attribute */
		uint32_t cacheable		: 1;  /**< Cacheable memory attribute */
		uint32_t exec_never		: 1;  /**< Execute never flag (XN) */
		uint32_t domain			: 4;  /**< Domain field [8:5] */
		uint32_t impl_def		: 1;  /**< Implementation defined bit */
		uint32_t acc_perms10		: 2;  /**< Access permissions AP[1:0] */
		uint32_t tex			: 3;  /**< Type extension field [14:12] */
		uint32_t acc_perms2		: 1;  /**< Access permission AP[2] */
		uint32_t shared			: 1;  /**< Shareable attribute */
		uint32_t not_global		: 1;  /**< Not global flag (nG) */
		uint32_t zero			: 1;  /**< Should be zero */
		uint32_t non_sec		: 1;  /**< Non-secure bit (NS) */
		uint32_t base_address		: 12; /**< Section base address [31:20] */
	} l1_section_1m;
	/** @brief Reference to Level 2 page table */
	struct {
		uint32_t id			: 2;  /**< Entry type ID [1:0] */
		uint32_t zero0			: 1;  /**< Should be zero (PXN if available) */
		uint32_t non_sec		: 1;  /**< Non-secure bit (NS) */
		uint32_t zero1			: 1;  /**< Should be zero */
		uint32_t domain			: 4;  /**< Domain field [8:5] */
		uint32_t impl_def		: 1;  /**< Implementation defined bit */
		uint32_t l2_page_table_address	: 22; /**< L2 page table address [31:10] */
	} l2_page_table_ref;
	/** @brief Undefined/invalid entry */
	struct {
		uint32_t id			: 2;  /**< Entry type ID [1:0] */
		uint32_t reserved		: 30; /**< Reserved bits [31:2] */
	} undefined;
	uint32_t word;  /**< Raw 32-bit word value */
};

/**
 * @brief Level 1 page table
 *
 * Contains the complete L1 page table with all entries.
 */
struct arm_mmu_l1_page_table {
	union arm_mmu_l1_page_table_entry entries[ARM_MMU_PT_L1_NUM_ENTRIES];  /**< L1 page table entries */
};

/**
 * @brief Level 2 page table entry
 *
 * Union representing a Level 2 (L2) page table entry with different
 * page size options (4KB and 64KB pages).
 */
union arm_mmu_l2_page_table_entry {
	/** @brief 4KB small page mapping */
	struct {
		uint32_t id			: 2;  /**< Entry type ID [1:0] */
		uint32_t bufferable		: 1;  /**< Bufferable memory attribute */
		uint32_t cacheable		: 1;  /**< Cacheable memory attribute */
		uint32_t acc_perms10		: 2;  /**< Access permissions AP[1:0] */
		uint32_t tex			: 3;  /**< Type extension field [8:6] */
		uint32_t acc_perms2		: 1;  /**< Access permission AP[2] */
		uint32_t shared			: 1;  /**< Shareable attribute */
		uint32_t not_global		: 1;  /**< Not global flag (nG) */
		uint32_t pa_base		: 20; /**< Physical address base [31:12] */
	} l2_page_4k;
	/** @brief 64KB large page mapping */
	struct {
		uint32_t id			: 2;  /**< Entry type ID [1:0] */
		uint32_t bufferable		: 1;  /**< Bufferable memory attribute */
		uint32_t cacheable		: 1;  /**< Cacheable memory attribute */
		uint32_t acc_perms10		: 2;  /**< Access permissions AP[1:0] */
		uint32_t zero			: 3;  /**< Should be zero [8:6] */
		uint32_t acc_perms2		: 1;  /**< Access permission AP[2] */
		uint32_t shared			: 1;  /**< Shareable attribute */
		uint32_t not_global		: 1;  /**< Not global flag (nG) */
		uint32_t tex			: 3;  /**< Type extension field [14:12] */
		uint32_t exec_never		: 1;  /**< Execute never flag (XN) */
		uint32_t pa_base		: 16; /**< Physical address base [31:16] */
	} l2_page_64k;
	/** @brief Undefined/invalid entry */
	struct {
		uint32_t id			: 2;  /**< Entry type ID [1:0] */
		uint32_t reserved		: 30; /**< Reserved bits [31:2] */
	} undefined;
	uint32_t word;  /**< Raw 32-bit word value */
};

/**
 * @brief Level 2 page table
 *
 * Contains the complete L2 page table with all entries.
 */
struct arm_mmu_l2_page_table {
	union arm_mmu_l2_page_table_entry entries[ARM_MMU_PT_L2_NUM_ENTRIES];  /**< L2 page table entries */
};

/**
 * @brief Level 2 page table usage tracking
 *
 * Data structure for tracking L2 table usage, contains an L1 index
 * reference if the respective L2 table is in use.
 */
struct arm_mmu_l2_page_table_status {
	uint32_t l1_index : 12;  /**< L1 page table index reference */
	uint32_t entries  : 9;   /**< Number of entries in use */
	uint32_t reserved : 11;  /**< Reserved for future use */
};

/**
 * @brief Flat memory range descriptor
 *
 * Data structure used to describe memory areas defined by the current
 * Zephyr image, for which an identity mapping (pa = va) will be set up.
 * These memory areas are processed during MMU initialization.
 */
struct arm_mmu_flat_range {
	const char	*name;   /**< Descriptive name of the memory range */
	uint32_t	start;   /**< Start address of the range */
	uint32_t	end;     /**< End address of the range */
	uint32_t	attrs;   /**< Memory attributes for the range */
};

/*
 * Data structure containing the memory attributes and permissions
 * data derived from a memory region's attr flags word in the format
 * required for setting up the corresponding PTEs.
 */
struct arm_mmu_perms_attrs {
	uint32_t acc_perms	: 2;
	uint32_t bufferable	: 1;
	uint32_t cacheable	: 1;
	uint32_t not_global	: 1;
	uint32_t non_sec	: 1;
	uint32_t shared		: 1;
	uint32_t tex		: 3;
	uint32_t exec_never	: 1;
	uint32_t id_mask	: 2;
	uint32_t domain		: 4;
	uint32_t reserved	: 15;
};

#endif /* ZEPHYR_ARCH_AARCH32_ARM_MMU_PRIV_H_ */
