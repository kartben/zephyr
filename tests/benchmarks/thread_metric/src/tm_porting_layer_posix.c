/***************************************************************************
 * Copyright (c) 2024 Microsoft Corporation
 * Copyright (c) 2026 Intel Corporation
 *
 * This program and the accompanying materials are made available under the
 * terms of the MIT License which is available at
 * https://opensource.org/licenses/MIT.
 *
 * SPDX-License-Identifier: MIT
 **************************************************************************/

/**************************************************************************/
/**************************************************************************/
/**                                                                       */
/** Thread-Metric Component                                               */
/**                                                                       */
/**   Porting Layer (POSIX API)                                           */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/

/* Include necessary files.  */

#include "tm_api.h"

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>

#define TM_TEST_NUM_THREADS        10
#define TM_TEST_STACK_SIZE         MAX(1024, PTHREAD_STACK_MIN)
#define TM_TEST_NUM_SEMAPHORES     4
#define TM_TEST_NUM_MESSAGE_QUEUES 4
#define TM_TEST_MSG_SIZE           16
#define TM_TEST_MSG_DEPTH          8

#if (CONFIG_MP_MAX_NUM_CPUS > 1)
#error "*** Tests are only designed for single processor systems! ***"
#endif

static pthread_t test_thread[TM_TEST_NUM_THREADS];
static K_THREAD_STACK_ARRAY_DEFINE(test_stack, TM_TEST_NUM_THREADS, TM_TEST_STACK_SIZE);
static void (*test_thread_entry[TM_TEST_NUM_THREADS])(void *, void *, void *);

/*
 * POSIX has no thread suspend/resume. Threads are created "suspended" by
 * blocking on a per-thread semaphore; tm_thread_resume() posts it and
 * tm_thread_suspend() (only ever called on self) waits on it.
 */
static sem_t suspend_sem[TM_TEST_NUM_THREADS];

static sem_t test_sem[TM_TEST_NUM_SEMAPHORES];

static mqd_t test_mq[TM_TEST_NUM_MESSAGE_QUEUES];

/*
 * This function called from main performs basic RTOS initialization,
 * calls the test initialization function, and then starts the RTOS function.
 */
void tm_initialize(void (*test_initialization_function)(void))
{
	test_initialization_function();
}

static void *test_thread_wrapper(void *arg)
{
	int thread_id = (int)(uintptr_t)arg;

	(void)sem_wait(&suspend_sem[thread_id]);
	test_thread_entry[thread_id](NULL, NULL, NULL);

	return NULL;
}

/*
 * This function takes a thread ID and priority and attempts to create the
 * file in the underlying RTOS.  Valid priorities range from 1 through 31,
 * where 1 is the highest priority and 31 is the lowest. If successful,
 * the function should return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_thread_create(int thread_id, int priority, void (*entry_function)(void *, void *, void *))
{
	int ret;
	pthread_attr_t attr;
	struct sched_param param;

	/* Thread-Metric priorities are inverted with respect to POSIX */
	param.sched_priority = sched_get_priority_max(SCHED_RR) - priority;

	ret = sem_init(&suspend_sem[thread_id], 0, 0);
	ret |= pthread_attr_init(&attr);
	ret |= pthread_attr_setstack(&attr, test_stack[thread_id], TM_TEST_STACK_SIZE);
	ret |= pthread_attr_setschedpolicy(&attr, SCHED_RR);
	ret |= pthread_attr_setschedparam(&attr, &param);
	ret |= pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

	test_thread_entry[thread_id] = entry_function;

	ret |= pthread_create(&test_thread[thread_id], &attr, test_thread_wrapper,
			      (void *)(uintptr_t)thread_id);

	return (ret == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function resumes the specified thread.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_thread_resume(int thread_id)
{
	return (sem_post(&suspend_sem[thread_id]) == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function suspends the specified thread.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_thread_suspend(int thread_id)
{
	return (sem_wait(&suspend_sem[thread_id]) == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function relinquishes to other ready threads at the same
 * priority.
 */
void tm_thread_relinquish(void)
{
	sched_yield();
}

/*
 * This function suspends the specified thread for the specified number
 * of seconds.
 */
void tm_thread_sleep(int seconds)
{
	struct timespec ts = {.tv_sec = seconds};

	nanosleep(&ts, NULL);
}

/*
 * This function creates the specified queue.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_queue_create(int queue_id)
{
	char name[16];
	struct mq_attr attr = {
		.mq_maxmsg = TM_TEST_MSG_DEPTH,
		.mq_msgsize = TM_TEST_MSG_SIZE,
	};

	snprintf(name, sizeof(name), "/tm_mq%d", queue_id);
	test_mq[queue_id] = mq_open(name, O_CREAT | O_RDWR, 0600, &attr);

	return (test_mq[queue_id] == (mqd_t)-1) ? TM_ERROR : TM_SUCCESS;
}

/*
 * This function sends a 16-byte message to the specified queue.  If successful,
 * the function should return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_queue_send(int queue_id, unsigned long *message_ptr)
{
	return (mq_send(test_mq[queue_id], (const char *)message_ptr, TM_TEST_MSG_SIZE, 0) == 0)
		       ? TM_SUCCESS
		       : TM_ERROR;
}

/*
 * This function receives a 16-byte message from the specified queue.  If successful,
 * the function should return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_queue_receive(int queue_id, unsigned long *message_ptr)
{
	return (mq_receive(test_mq[queue_id], (char *)message_ptr, TM_TEST_MSG_SIZE, NULL) ==
		TM_TEST_MSG_SIZE)
		       ? TM_SUCCESS
		       : TM_ERROR;
}

/*
 * This function creates the specified semaphore.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_semaphore_create(int semaphore_id)
{
	return (sem_init(&test_sem[semaphore_id], 0, 1) == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function gets the specified semaphore.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_semaphore_get(int semaphore_id)
{
	return (sem_trywait(&test_sem[semaphore_id]) == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function puts the specified semaphore.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_semaphore_put(int semaphore_id)
{
	return (sem_post(&test_sem[semaphore_id]) == 0) ? TM_SUCCESS : TM_ERROR;
}

/* This function is defined by the benchmark. */
extern void tm_interrupt_handler(const void *);

void tm_cause_interrupt(void)
{
	irq_offload(tm_interrupt_handler, NULL);
}

/*
 * This function creates the specified memory pool that can support one or more
 * allocations of 128 bytes.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_memory_pool_create(int pool_id)
{
	ARG_UNUSED(pool_id);

	return TM_SUCCESS;
}

/*
 * This function allocates a 128 byte block from the specified memory pool.
 * If successful, the function should return TM_SUCCESS. Otherwise, TM_ERROR
 * should be returned.
 */
int tm_memory_pool_allocate(int pool_id, unsigned char **memory_ptr)
{
	ARG_UNUSED(pool_id);

	*memory_ptr = malloc(128);

	return (*memory_ptr != NULL) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function releases a previously allocated 128 byte block from the specified
 * memory pool. If successful, the function should return TM_SUCCESS. Otherwise, TM_ERROR
 * should be returned.
 */
int tm_memory_pool_deallocate(int pool_id, unsigned char *memory_ptr)
{
	ARG_UNUSED(pool_id);

	free(memory_ptr);

	return TM_SUCCESS;
}
