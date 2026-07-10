/***************************************************************************
 * Copyright (c) 2024 Microsoft Corporation
 * Copyright (c) 2024 Intel Corporation
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
/**   Porting Layer (POSIX API flavor)                                    */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/

/*
 * This porting layer implements the RTOS-neutral Thread-Metric API on top of
 * Zephyr's POSIX API (pthreads, POSIX semaphores, and POSIX message queues)
 * instead of the native Zephyr kernel API. Running the same benchmarks through
 * this layer captures the additional overhead introduced by the POSIX
 * abstraction relative to the native Zephyr porting layer.
 *
 * To keep the comparison fair, the POSIX SCHED_RR scheduling priority handed to
 * each thread is chosen so that the resulting Zephyr thread priority matches the
 * value that the native porting layer would have used.
 */

/* Include necessary files.  */

#include "tm_api.h"

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <zephyr/kernel.h>

#define TM_TEST_NUM_THREADS        10
#define TM_TEST_STACK_SIZE         K_THREAD_STACK_LEN(2048)
#define TM_TEST_NUM_SEMAPHORES     4
#define TM_TEST_NUM_MESSAGE_QUEUES 4

/* Thread-Metric messages are always 16 bytes long. */
#define TM_MESSAGE_SIZE    16
/* Each message queue holds at least one 16-byte message. */
#define TM_QUEUE_DEPTH     8
/* Each memory pool serves 128-byte blocks. */
#define TM_POOL_BLOCK_SIZE 128

#if (CONFIG_MP_MAX_NUM_CPUS > 1)
#error "*** Tests are only designed for single processor systems! ***"
#endif

/*
 * Each test thread carries a "run" semaphore that emulates the create-suspended,
 * resume, and self-suspend semantics expected by the benchmarks: a freshly
 * created thread blocks on the semaphore until it is resumed, and self-suspends
 * by taking it again.
 */
struct tm_posix_thread {
	pthread_t tid;
	sem_t run_sem;
	void (*entry)(void *, void *, void *);
};

static struct tm_posix_thread test_thread[TM_TEST_NUM_THREADS];
static K_THREAD_STACK_ARRAY_DEFINE(test_stack, TM_TEST_NUM_THREADS, TM_TEST_STACK_SIZE);

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

/* Common entry trampoline: block until resumed, then run the test entry. */
static void *tm_thread_trampoline(void *arg)
{
	struct tm_posix_thread *thread = arg;

	/* Threads are created in the suspended state. Wait for the first resume. */
	sem_wait(&thread->run_sem);

	thread->entry(NULL, NULL, NULL);

	return NULL;
}

/*
 * This function takes a thread ID and priority and attempts to create the
 * thread in the underlying RTOS.  Valid priorities range from 1 through 31,
 * where 1 is the highest priority and 31 is the lowest. If successful,
 * the function should return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_thread_create(int thread_id, int priority, void (*entry_function)(void *, void *, void *))
{
	struct tm_posix_thread *thread = &test_thread[thread_id];
	pthread_attr_t attr;
	struct sched_param param;
	int ret;

	thread->entry = entry_function;

	/* Start with the run semaphore empty so the thread is suspended. */
	if (sem_init(&thread->run_sem, 0, 0) != 0) {
		return TM_ERROR;
	}

	ret = pthread_attr_init(&attr);
	if (ret != 0) {
		return TM_ERROR;
	}

	(void)pthread_attr_setstack(&attr, test_stack[thread_id], TM_TEST_STACK_SIZE);

	/*
	 * Use SCHED_RR and pick the POSIX priority such that the resulting Zephyr
	 * thread priority equals the RTOS-neutral priority requested by the test.
	 * This keeps the scheduling behavior identical to the native porting layer
	 * so that only the POSIX API overhead is measured.
	 */
	(void)pthread_attr_setschedpolicy(&attr, SCHED_RR);
	param.sched_priority = sched_get_priority_max(SCHED_RR) - priority;
	(void)pthread_attr_setschedparam(&attr, &param);
	(void)pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

	ret = pthread_create(&thread->tid, &attr, tm_thread_trampoline, thread);

	(void)pthread_attr_destroy(&attr);

	return (ret == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function resumes the specified thread.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_thread_resume(int thread_id)
{
	return (sem_post(&test_thread[thread_id].run_sem) == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function suspends the specified thread.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 *
 * The benchmarks only ever self-suspend, so this simply blocks the calling thread
 * on its run semaphore until it is resumed again.
 */
int tm_thread_suspend(int thread_id)
{
	return (sem_wait(&test_thread[thread_id].run_sem) == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function relinquishes to other ready threads at the same
 * priority.
 */
void tm_thread_relinquish(void)
{
	(void)sched_yield();
}

/*
 * This function suspends the calling thread for the specified number
 * of seconds.
 */
void tm_thread_sleep(int seconds)
{
	(void)sleep((unsigned int)seconds);
}

/*
 * This function creates the specified queue.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_queue_create(int queue_id)
{
	struct mq_attr attr = {
		.mq_maxmsg = TM_QUEUE_DEPTH,
		.mq_msgsize = TM_MESSAGE_SIZE,
		.mq_flags = 0,
	};
	char name[16];

	(void)snprintf(name, sizeof(name), "/tm_mq%d", queue_id);

	test_mq[queue_id] = mq_open(name, O_CREAT | O_RDWR, 0600, &attr);

	return (test_mq[queue_id] != (mqd_t)-1) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function sends a 16-byte message to the specified queue.  If successful,
 * the function should return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_queue_send(int queue_id, unsigned long *message_ptr)
{
	int ret = mq_send(test_mq[queue_id], (const char *)message_ptr, TM_MESSAGE_SIZE, 0);

	return (ret == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function receives a 16-byte message from the specified queue.  If successful,
 * the function should return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_queue_receive(int queue_id, unsigned long *message_ptr)
{
	ssize_t ret = mq_receive(test_mq[queue_id], (char *)message_ptr, TM_MESSAGE_SIZE, NULL);

	return (ret == TM_MESSAGE_SIZE) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function creates the specified semaphore.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_semaphore_create(int semaphore_id)
{
	/* Create an available binary semaphore (initial count of 1). */
	return (sem_init(&test_sem[semaphore_id], 0, 1) == 0) ? TM_SUCCESS : TM_ERROR;
}

/*
 * This function gets the specified semaphore.  If successful, the function should
 * return TM_SUCCESS. Otherwise, TM_ERROR should be returned.
 */
int tm_semaphore_get(int semaphore_id)
{
	/* Non-blocking take to match the native K_NO_WAIT behavior. */
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
 *
 * The POSIX API has no fixed-block pool primitive, so allocations are served
 * directly from the libc heap via malloc()/free().
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

	*memory_ptr = malloc(TM_POOL_BLOCK_SIZE);

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
