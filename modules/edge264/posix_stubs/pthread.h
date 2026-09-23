/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal pthread stubs so edge264 can be built with n_threads = 0.
 * Condition variables and mutexes are no-ops; thread creation fails.
 */

#ifndef EDGE264_POSIX_STUBS_PTHREAD_H
#define EDGE264_POSIX_STUBS_PTHREAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int pthread_t;
typedef struct {
	int unused;
} pthread_mutex_t;
typedef struct {
	int unused;
} pthread_cond_t;
typedef struct {
	int unused;
} pthread_mutexattr_t;
typedef struct {
	int unused;
} pthread_condattr_t;
typedef struct {
	int unused;
} pthread_attr_t;

static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
	(void)m;
	(void)a;
	return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{
	(void)m;
	return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *m)
{
	(void)m;
	return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{
	(void)m;
	return 0;
}

static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
	(void)c;
	(void)a;
	return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t *c)
{
	(void)c;
	return 0;
}

static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	(void)c;
	(void)m;
	return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *c)
{
	(void)c;
	return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *c)
{
	(void)c;
	return 0;
}

static inline int pthread_create(pthread_t *t, const pthread_attr_t *a,
				  void *(*fn)(void *), void *arg)
{
	(void)t;
	(void)a;
	(void)fn;
	(void)arg;
	return -1;
}

static inline int pthread_cancel(pthread_t t)
{
	(void)t;
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* EDGE264_POSIX_STUBS_PTHREAD_H */
