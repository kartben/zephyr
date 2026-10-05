/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Force-included in the ESP-DL sources: routes the ESP-IDF heap_caps
 * allocation API to the heaps of esp_dl_heap.c.
 */

#ifndef ESP_DL_ZEPHYR_HEAP_H_
#define ESP_DL_ZEPHYR_HEAP_H_

#include <stddef.h>
#include <stdint.h>
#include <esp_heap_caps.h>

#ifdef __cplusplus
extern "C" {
#endif

void *esp_dl_heap_caps_malloc(size_t size, uint32_t caps);
void *esp_dl_heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *esp_dl_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void *esp_dl_heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps);
size_t esp_dl_heap_caps_get_free_size(uint32_t caps);
size_t esp_dl_heap_caps_get_largest_free_block(uint32_t caps);
void esp_dl_heap_caps_free(void *ptr);

#ifdef __cplusplus
}
#endif

#define heap_caps_malloc                 esp_dl_heap_caps_malloc
#define heap_caps_calloc                 esp_dl_heap_caps_calloc
#define heap_caps_aligned_alloc          esp_dl_heap_caps_aligned_alloc
#define heap_caps_aligned_calloc         esp_dl_heap_caps_aligned_calloc
#define heap_caps_get_free_size          esp_dl_heap_caps_get_free_size
#define heap_caps_get_largest_free_block esp_dl_heap_caps_get_largest_free_block
#define heap_caps_free                   esp_dl_heap_caps_free

#endif /* ESP_DL_ZEPHYR_HEAP_H_ */
