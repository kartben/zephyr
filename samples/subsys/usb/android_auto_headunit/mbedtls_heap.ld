/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The TLS heap is tens of kilobytes that only the processor ever touches, and
 * a serial boot image has little more than half a megabyte for everything.
 * Put it in the external RAM that already holds the framebuffer and the
 * decoder's larger arena, and leave the image's own RAM to the code.
 */
SECTION_DATA_PROLOGUE(_MBEDTLS_HEAP_SECTION, (NOLOAD),)
{
	*(.mbedtls_heap)
#ifdef CONFIG_SOC_SERIES_STM32N6X
} GROUP_LINK_IN(PSRAM)
#else
} GROUP_LINK_IN(SDRAM)
#endif
