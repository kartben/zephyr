/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Demonstrate scope-based cleanup helpers:
 *
 *  1. scope_guard  — automatically lock/unlock a mutex
 *  2. scope_defer  — automatically free heap memory
 *  3. scope_var    — custom scoped variable (buffer with RAII-style lifecycle)
 */

#include <zephyr/kernel.h>
#include <zephyr/cleanup/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(cleanup_sample, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------
 * 1. Scoped mutex guard
 * ---------------------------------------------------------------------- */

static K_MUTEX_DEFINE(shared_lock);
static int shared_counter;

static void increment_counter(int amount)
{
	/*
	 * scope_guard(k_mutex) locks the mutex immediately and unlocks it
	 * when the guard variable leaves scope — even on early return or error
	 * paths that branch out of this function.
	 */
	scope_guard(k_mutex)(&shared_lock);

	shared_counter += amount;
	LOG_INF("[guard] counter = %d (lock_count=%u)", shared_counter,
		shared_lock.lock_count);
}

/* -------------------------------------------------------------------------
 * 2. Scoped defer — automatic k_free
 * ---------------------------------------------------------------------- */

static void demo_defer_free(void)
{
	uint8_t *buf = k_malloc(64);

	if (!buf) {
		LOG_ERR("k_malloc failed");
		return;
	}

	/*
	 * Register automatic cleanup: k_free(buf) is called when buf_defer
	 * goes out of scope, regardless of which return path is taken.
	 */
	scope_defer(k_free)(buf);

	memset(buf, 0xAB, 64);
	LOG_INF("[defer] buffer allocated and filled (will be freed on scope exit)");

	/* Simulate early error exit — cleanup still happens */
	if (buf[0] != 0xAB) {
		LOG_ERR("unexpected buffer content");
		return;
	}

	LOG_INF("[defer] buffer work done");
	/* k_free(buf) called automatically here */
}

/* -------------------------------------------------------------------------
 * 3. Custom scoped variable
 * ---------------------------------------------------------------------- */

/*
 * A simple buffer type that tracks its allocation state.
 */
struct managed_buf {
	uint8_t *data;
	size_t   len;
};

static inline struct managed_buf managed_buf_init(size_t len)
{
	return (struct managed_buf){
		.data = k_malloc(len),
		.len  = len,
	};
}

static inline void managed_buf_exit(struct managed_buf b)
{
	if (b.data) {
		LOG_INF("[scope_var] freeing %zu-byte buffer", b.len);
		k_free(b.data);
	}
}

SCOPE_VAR_DEFINE(managed_buf, struct managed_buf,
		 managed_buf_exit(_T),
		 managed_buf_init(len),
		 size_t len);

static void demo_scoped_var(void)
{
	/*
	 * scope_var allocates a managed_buf and registers managed_buf_exit
	 * to run when 'mb' goes out of scope.
	 */
	scope_var(managed_buf, mb)(128);

	if (!mb.data) {
		LOG_ERR("[scope_var] allocation failed");
		return;
	}

	LOG_INF("[scope_var] allocated %zu-byte buffer at %p", mb.len, mb.data);
	memset(mb.data, 0, mb.len);
	mb.data[0] = 42;
	LOG_INF("[scope_var] wrote sentinel byte %d", mb.data[0]);

	/* managed_buf_exit(&mb) called automatically here */
}

/* -------------------------------------------------------------------------
 * 4. LIFO cleanup order demonstration
 * ---------------------------------------------------------------------- */

static K_MUTEX_DEFINE(outer_lock);
static K_MUTEX_DEFINE(inner_lock);

static void demo_lifo_order(void)
{
	LOG_INF("[lifo] acquiring outer lock");
	scope_guard(k_mutex)(&outer_lock);

	LOG_INF("[lifo] acquiring inner lock");
	scope_guard(k_mutex)(&inner_lock);

	LOG_INF("[lifo] both locks held; leaving scope");
	/*
	 * Cleanup order is LIFO: inner_lock is released first,
	 * then outer_lock.
	 */
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(void)
{
	LOG_INF("Scope-based cleanup helpers sample");

	LOG_INF("--- 1. Mutex guard ---");
	increment_counter(10);
	increment_counter(5);
	LOG_INF("    final counter: %d", shared_counter);

	LOG_INF("--- 2. Deferred k_free ---");
	demo_defer_free();

	LOG_INF("--- 3. Custom scoped variable ---");
	demo_scoped_var();

	LOG_INF("--- 4. LIFO cleanup order ---");
	demo_lifo_order();
	LOG_INF("    both locks released in reverse order");

	LOG_INF("Sample complete");
	return 0;
}
