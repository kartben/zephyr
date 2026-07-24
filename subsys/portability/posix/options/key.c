/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2023 Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "posix_internal.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/posix/pthread.h>
#include <zephyr/sys/bitarray.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/sem.h>

LOG_MODULE_REGISTER(pthread_key, CONFIG_PTHREAD_KEY_LOG_LEVEL);

SYS_SEM_DEFINE(pthread_key_lock, 1, 1);

/* This is non-standard (i.e. an implementation detail) */
#define PTHREAD_KEY_INITIALIZER (-1)

/*
 * We reserve the MSB to mark a pthread_key_t as initialized (from the
 * perspective of the application). With a linear space, this means that
 * the theoretical pthread_key_t range is [0,2147483647].
 */
BUILD_ASSERT(CONFIG_POSIX_THREAD_KEYS_MAX < PTHREAD_OBJ_MASK_INIT,
	     "CONFIG_POSIX_THREAD_KEYS_MAX is too high");

static pthread_key_obj posix_key_pool[CONFIG_POSIX_THREAD_KEYS_MAX];
SYS_BITARRAY_DEFINE_STATIC(posix_key_bitarray, CONFIG_POSIX_THREAD_KEYS_MAX);

static inline size_t posix_key_to_offset(pthread_key_obj *k)
{
	return k - posix_key_pool;
}

static inline size_t to_posix_key_idx(pthread_key_t key)
{
	return mark_pthread_obj_uninitialized(key);
}

static pthread_key_obj *get_posix_key(pthread_key_t key)
{
	int actually_initialized;
	size_t bit = to_posix_key_idx(key);

	if (!is_pthread_obj_initialized(key)) {
		LOG_DBG("Key is uninitialized (%x)", key);
		return NULL;
	}

	if (sys_bitarray_test_bit(&posix_key_bitarray, bit, &actually_initialized) < 0) {
		LOG_DBG("Key is invalid (%x)", key);
		return NULL;
	}

	if (actually_initialized == 0) {
		LOG_DBG("Key claims to be initialized (%x)", key);
		return NULL;
	}

	return &posix_key_pool[bit];
}

static pthread_key_obj *to_posix_key(pthread_key_t *key)
{
	size_t bit;
	pthread_key_obj *k;

	if (*key != PTHREAD_KEY_INITIALIZER) {
		return get_posix_key(*key);
	}

	if (sys_bitarray_alloc(&posix_key_bitarray, 1, &bit) < 0) {
		return NULL;
	}

	*key = mark_pthread_obj_initialized(bit);
	k = &posix_key_pool[bit];
	memset(k, 0, sizeof(*k));

	return k;
}

static void key_data_unlink(pthread_key_obj *key_obj, struct pthread_key_data *key_data)
{
	struct posix_thread *thread = key_data->thread;
	size_t key_idx = key_data->key_idx;

	sys_dlist_remove(&key_data->node);
	thread->key_data[key_idx] = NULL;
	thread->key_values[key_idx] = NULL;
	k_free(key_data);
}

/**
 * @brief Create a key for thread-specific data
 *
 * See IEEE 1003.1
 */
int pthread_key_create(pthread_key_t *key,
		void (*destructor)(void *))
{
	pthread_key_obj *new_key;

	*key = PTHREAD_KEY_INITIALIZER;
	new_key = to_posix_key(key);
	if (new_key == NULL) {
		return ENOMEM;
	}

	sys_dlist_init(&new_key->key_data_l);
	new_key->destructor = destructor;
	LOG_DBG("Initialized key %p (%x)", new_key, *key);

	return 0;
}

/**
 * @brief Delete a key for thread-specific data
 *
 * See IEEE 1003.1
 */
int pthread_key_delete(pthread_key_t key)
{
	size_t bit;
	int ret = EINVAL;
	pthread_key_obj *key_obj = NULL;
	struct pthread_key_data *key_data;
	sys_dnode_t *node_l, *next_node_l;

	SYS_SEM_LOCK(&pthread_key_lock) {
		key_obj = get_posix_key(key);
		if (key_obj == NULL) {
			ret = EINVAL;
			SYS_SEM_LOCK_BREAK;
		}

		SYS_DLIST_FOR_EACH_NODE_SAFE(&key_obj->key_data_l, node_l, next_node_l) {
			key_data = CONTAINER_OF(node_l, struct pthread_key_data, node);
			key_data_unlink(key_obj, key_data);
		}

		bit = posix_key_to_offset(key_obj);
		ret = sys_bitarray_free(&posix_key_bitarray, 1, bit);
		__ASSERT_NO_MSG(ret == 0);
	}

	if (ret == 0) {
		LOG_DBG("Deleted key %p (%x)", key_obj, key);
	}

	return ret;
}

/**
 * @brief Associate a thread-specific value with a key
 *
 * See IEEE 1003.1
 */
int pthread_setspecific(pthread_key_t key, const void *value)
{
	pthread_key_obj *key_obj;
	struct posix_thread *thread;
	struct pthread_key_data *key_data;
	size_t key_idx;
	int retval = 0;

	thread = to_posix_thread(pthread_self());
	if (thread == NULL) {
		return EINVAL;
	}

	key_obj = get_posix_key(key);
	if (key_obj == NULL) {
		return EINVAL;
	}

	key_idx = to_posix_key_idx(key);
	if (key_idx >= CONFIG_POSIX_THREAD_KEYS_MAX) {
		return EINVAL;
	}

	if (thread->key_data[key_idx] != NULL) {
		thread->key_values[key_idx] = (void *)value;
		return 0;
	}

	SYS_SEM_LOCK(&pthread_key_lock) {
		if (thread->key_data[key_idx] != NULL) {
			thread->key_values[key_idx] = (void *)value;
			SYS_SEM_LOCK_BREAK;
		}

		key_data = k_malloc(sizeof(struct pthread_key_data));
		if (key_data == NULL) {
			retval = ENOMEM;
			SYS_SEM_LOCK_BREAK;
		}

		key_data->thread = thread;
		key_data->key_idx = key_idx;
		thread->key_values[key_idx] = (void *)value;
		thread->key_data[key_idx] = key_data;
		sys_dlist_append(&key_obj->key_data_l, &key_data->node);
	}

	return retval;
}

/**
 * @brief Get the thread-specific value associated with the key
 *
 * See IEEE 1003.1
 */
void *pthread_getspecific(pthread_key_t key)
{
	struct posix_thread *thread;
	size_t key_idx;

	thread = to_posix_thread(pthread_self());
	if (thread == NULL) {
		return NULL;
	}

	if (!is_pthread_obj_initialized(key)) {
		return NULL;
	}

	key_idx = to_posix_key_idx(key);
	if (key_idx >= CONFIG_POSIX_THREAD_KEYS_MAX || get_posix_key(key) == NULL) {
		return NULL;
	}

	if (thread->key_data[key_idx] == NULL) {
		return NULL;
	}

	return thread->key_values[key_idx];
}

void posix_key_thread_finalize(struct posix_thread *t)
{
	pthread_key_obj *key_obj;
	struct pthread_key_data *key_data;

	for (size_t key_idx = 0; key_idx < CONFIG_POSIX_THREAD_KEYS_MAX; key_idx++) {
		key_data = t->key_data[key_idx];
		if (key_data == NULL) {
			continue;
		}

		key_obj = &posix_key_pool[key_idx];
		if (key_obj->destructor != NULL) {
			key_obj->destructor(t->key_values[key_idx]);
		}

		SYS_SEM_LOCK(&pthread_key_lock) {
			sys_dlist_remove(&key_data->node);
			t->key_data[key_idx] = NULL;
			t->key_values[key_idx] = NULL;
			k_free(key_data);
		}
	}
}