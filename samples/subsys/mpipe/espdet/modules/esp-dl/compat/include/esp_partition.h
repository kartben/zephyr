/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Minimal ESP-IDF partition API for ESP-DL. Models are embedded in the
 * application image on Zephyr, so a model in a flash partition is never found.
 */

#ifndef ESP_DL_COMPAT_ESP_PARTITION_H_
#define ESP_DL_COMPAT_ESP_PARTITION_H_

#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ESP_PARTITION_TYPE_APP = 0x00,
	ESP_PARTITION_TYPE_DATA = 0x01,
	ESP_PARTITION_TYPE_ANY = 0xff,
} esp_partition_type_t;

typedef enum {
	ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

typedef enum {
	ESP_PARTITION_MMAP_DATA,
	ESP_PARTITION_MMAP_INST,
} esp_partition_mmap_memory_t;

typedef uint32_t esp_partition_mmap_handle_t;

typedef struct {
	esp_partition_type_t type;
	esp_partition_subtype_t subtype;
	uint32_t address;
	uint32_t size;
	char label[17];
} esp_partition_t;

static inline const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
							      esp_partition_subtype_t subtype,
							      const char *label)
{
	(void)type;
	(void)subtype;
	(void)label;

	return NULL;
}

static inline esp_err_t esp_partition_mmap(const esp_partition_t *partition, size_t offset,
					   size_t size, esp_partition_mmap_memory_t memory,
					   const void **out_ptr,
					   esp_partition_mmap_handle_t *out_handle)
{
	(void)partition;
	(void)offset;
	(void)size;
	(void)memory;
	(void)out_ptr;
	(void)out_handle;

	return ESP_ERR_NOT_SUPPORTED;
}

static inline void esp_partition_munmap(esp_partition_mmap_handle_t handle)
{
	(void)handle;
}

#ifdef __cplusplus
}
#endif

#endif /* ESP_DL_COMPAT_ESP_PARTITION_H_ */
