/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ESP-IDF heap_caps allocator for ESP-DL.
 *
 * ESP-DL asks for internal or external (PSRAM) memory through the heap_caps
 * capability bits. The heap_caps port of hal_espressif serves every request
 * from the system heap, so ESP-DL gets two heaps of its own instead: one in
 * internal SRAM and one carved out of the PSRAM shared multi-heap.
 */

#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/sys/math_extras.h>
#include <zephyr/sys/sys_heap.h>

#include <esp_heap_caps.h>

LOG_MODULE_REGISTER(esp_dl_heap, LOG_LEVEL_INF);

static uint8_t esp_dl_int_mem[CONFIG_ESP_DL_INTERNAL_HEAP_SIZE] __aligned(16);
static struct k_heap esp_dl_int_heap;

static uint8_t *esp_dl_ext_mem;
static struct k_heap esp_dl_ext_heap;

static bool esp_dl_in_heap(const uint8_t *mem, size_t size, const void *ptr)
{
	return (mem != NULL) && ((const uint8_t *)ptr >= mem) &&
	       ((const uint8_t *)ptr < (mem + size));
}

static struct k_heap *esp_dl_heap_of(const void *ptr)
{
	if (esp_dl_in_heap(esp_dl_int_mem, sizeof(esp_dl_int_mem), ptr)) {
		return &esp_dl_int_heap;
	}

	if (esp_dl_in_heap(esp_dl_ext_mem, CONFIG_ESP_DL_PSRAM_HEAP_SIZE, ptr)) {
		return &esp_dl_ext_heap;
	}

	return NULL;
}

static bool esp_dl_wants_internal(uint32_t caps)
{
	return (caps & (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)) != 0U;
}

void *esp_dl_heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
	void *ptr = NULL;

	if (alignment < sizeof(void *)) {
		alignment = sizeof(void *);
	}

	if (esp_dl_wants_internal(caps)) {
		return k_heap_aligned_alloc(&esp_dl_int_heap, alignment, size, K_NO_WAIT);
	}

	/* Anything else goes to PSRAM, falling back to internal SRAM unless PSRAM is required */
	if (esp_dl_ext_mem != NULL) {
		ptr = k_heap_aligned_alloc(&esp_dl_ext_heap, alignment, size, K_NO_WAIT);
	}

	if ((ptr == NULL) && ((caps & MALLOC_CAP_SPIRAM) == 0U)) {
		ptr = k_heap_aligned_alloc(&esp_dl_int_heap, alignment, size, K_NO_WAIT);
	}

	return ptr;
}

void *esp_dl_heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps)
{
	size_t total;
	void *ptr;

	if (size_mul_overflow(n, size, &total)) {
		return NULL;
	}

	ptr = esp_dl_heap_caps_aligned_alloc(alignment, total, caps);
	if (ptr != NULL) {
		memset(ptr, 0, total);
	}

	return ptr;
}

void *esp_dl_heap_caps_malloc(size_t size, uint32_t caps)
{
	return esp_dl_heap_caps_aligned_alloc(sizeof(void *), size, caps);
}

void *esp_dl_heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
	return esp_dl_heap_caps_aligned_calloc(sizeof(void *), n, size, caps);
}

static size_t esp_dl_heap_free_bytes(struct k_heap *heap)
{
	struct sys_memory_stats stats;

	if (sys_heap_runtime_stats_get(&heap->heap, &stats) != 0) {
		return 0;
	}

	return stats.free_bytes;
}

size_t esp_dl_heap_caps_get_free_size(uint32_t caps)
{
	if ((caps & MALLOC_CAP_SPIRAM) != 0U) {
		return (esp_dl_ext_mem != NULL) ? esp_dl_heap_free_bytes(&esp_dl_ext_heap) : 0;
	}

	if (esp_dl_wants_internal(caps)) {
		return esp_dl_heap_free_bytes(&esp_dl_int_heap);
	}

	return esp_dl_heap_free_bytes(&esp_dl_int_heap) +
	       ((esp_dl_ext_mem != NULL) ? esp_dl_heap_free_bytes(&esp_dl_ext_heap) : 0);
}

/* sys_heap does not track its largest free block: report the free size as an upper bound */
size_t esp_dl_heap_caps_get_largest_free_block(uint32_t caps)
{
	return esp_dl_heap_caps_get_free_size(caps);
}

static bool esp_dl_heap_free(void *ptr)
{
	struct k_heap *heap = esp_dl_heap_of(ptr);

	if (heap == NULL) {
		return false;
	}

	k_heap_free(heap, ptr);

	return true;
}

void esp_dl_heap_caps_free(void *ptr)
{
	if (ptr != NULL && !esp_dl_heap_free(ptr)) {
		heap_caps_free(ptr);
	}
}

/*
 * ESP-IDF has a single heap, so ESP-DL also releases the memory it got from
 * heap_caps with free(). free() is wrapped at link time to hand the pointers of
 * the ESP-DL heaps back to them.
 */
void __real_free(void *ptr);

void __wrap_free(void *ptr)
{
	if (ptr != NULL && !esp_dl_heap_free(ptr)) {
		__real_free(ptr);
	}
}

static int esp_dl_heap_init(void)
{
	k_heap_init(&esp_dl_int_heap, esp_dl_int_mem, sizeof(esp_dl_int_mem));

	esp_dl_ext_mem = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 16,
							 CONFIG_ESP_DL_PSRAM_HEAP_SIZE);
	if (esp_dl_ext_mem == NULL) {
		LOG_ERR("Cannot reserve %u bytes of PSRAM", CONFIG_ESP_DL_PSRAM_HEAP_SIZE);
		return -ENOMEM;
	}

	k_heap_init(&esp_dl_ext_heap, esp_dl_ext_mem, CONFIG_ESP_DL_PSRAM_HEAP_SIZE);

	return 0;
}

SYS_INIT(esp_dl_heap_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
