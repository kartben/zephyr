/*
 * Copyright 2023 Linaro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/instrumentation/instrumentation.h>

#define SLEEPTIME 10
#define STACKSIZE 1024
#define PRIORITY  7

/* ====================================================================
 * Demo Scenario 1: Basic Thread Synchronization (Ping-Pong)
 * ====================================================================
 * This scenario demonstrates simple context switching between threads
 * using semaphores, which is useful for tracing thread interactions.
 */

void __no_optimization loop_0(void)
{
	/* Wait to spend some cycles */
	k_busy_wait(10);
}

void __no_optimization loop_1(void)
{
	/* Wait to spend some cycles */
	k_busy_wait(1000);
}

/*
 * 'main' thread can take its mutex promptly and run. 'thread_A' needs to wait 'main' give its
 * (thread_A) mutex to run.
 */
K_SEM_DEFINE(main_sem, 1, 1);		/* Initialized as ready to be taken */
K_SEM_DEFINE(thread_a_sem, 0, 1);	/* Initialized as already taken (blocked) */

K_THREAD_STACK_DEFINE(thread_a_stack, STACKSIZE);
static struct k_thread thread_a_data;

static int counter;

void get_sem_and_exec_function(struct k_sem *my_sem, struct k_sem *other_sem, void (*func)(void))
{
	while (counter < 4) {
		k_sem_take(my_sem, K_FOREVER);

		func();
		k_msleep(SLEEPTIME);

		counter++;
		k_sem_give(other_sem);
	}
}

void thread_A(void *notused0, void *notused1, void *notused2)
{
	ARG_UNUSED(notused0);
	ARG_UNUSED(notused1);
	ARG_UNUSED(notused2);

	get_sem_and_exec_function(&thread_a_sem, &main_sem, loop_0);
}

/* ====================================================================
 * Demo Scenario 2: Producer-Consumer Pattern
 * ====================================================================
 * This scenario demonstrates a classic producer-consumer pattern with
 * a message queue, showing inter-thread communication overhead.
 */

#define QUEUE_SIZE 10
K_MSGQ_DEFINE(demo_msgq, sizeof(uint32_t), QUEUE_SIZE, 4);

K_THREAD_STACK_DEFINE(producer_stack, STACKSIZE);
static struct k_thread producer_data;

K_THREAD_STACK_DEFINE(consumer_stack, STACKSIZE);
static struct k_thread consumer_data;

void __no_optimization process_data(uint32_t data)
{
	/* Simulate data processing */
	k_busy_wait(500 + (data % 100));
}

void producer_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t data = 0;

	for (int i = 0; i < 5; i++) {
		data = 100 + i;
		while (k_msgq_put(&demo_msgq, &data, K_NO_WAIT) != 0) {
			/* Queue full, wait and retry */
			k_msleep(5);
		}
		k_msleep(10);
	}
}

void consumer_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t received_data;

	for (int i = 0; i < 5; i++) {
		if (k_msgq_get(&demo_msgq, &received_data, K_FOREVER) == 0) {
			process_data(received_data);
		}
	}
}

/* ====================================================================
 * Demo Scenario 3: Recursive Function Profiling
 * ====================================================================
 * This scenario demonstrates profiling of recursive function calls,
 * useful for understanding call depth and execution time distribution.
 */

uint32_t __no_optimization fibonacci(uint32_t n)
{
	if (n <= 1) {
		return n;
	}
	return fibonacci(n - 1) + fibonacci(n - 2);
}

void __no_optimization compute_fibonacci(void)
{
	/* Calculate fibonacci numbers to create interesting call graphs */
	for (int i = 5; i <= 10; i++) {
		fibonacci(i);
		k_msleep(5);
	}
}

/* ====================================================================
 * Demo Scenario 4: Variable Workload Simulation
 * ====================================================================
 * This scenario demonstrates threads with varying workloads to show
 * how instrumentation captures different execution patterns.
 */

K_THREAD_STACK_DEFINE(worker_stack, STACKSIZE);
static struct k_thread worker_data;

void __no_optimization light_work(void)
{
	k_busy_wait(100);
}

void __no_optimization medium_work(void)
{
	k_busy_wait(500);
}

void __no_optimization heavy_work(void)
{
	k_busy_wait(2000);
}

void worker_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Execute different workloads */
	for (int i = 0; i < 3; i++) {
		light_work();
		k_msleep(5);
		medium_work();
		k_msleep(5);
		heavy_work();
		k_msleep(10);
	}
}

/* ====================================================================
 * Main Entry Point - Orchestrates All Demo Scenarios
 * ====================================================================
 */

int main(void)
{
	k_tid_t thread_a, producer, consumer, worker;

	printk("=================================================\n");
	printk("Instrumentation Subsystem Demo\n");
	printk("=================================================\n\n");

	printk("Starting Scenario 1: Basic Thread Synchronization\n");
	printk("--------------------------------------------------\n");

	/* Scenario 1: Create Thread A for ping-pong demo */
	thread_a = k_thread_create(&thread_a_data, thread_a_stack, STACKSIZE,
				   thread_A, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(thread_a, "thread_A");

	/* Start ping-pong between 'main' and 'thread_A' */
	get_sem_and_exec_function(&main_sem, &thread_a_sem, loop_1);

	printk("Scenario 1 completed\n\n");

	/* Wait for thread A to complete */
	k_msleep(100);

	printk("Starting Scenario 2: Producer-Consumer Pattern\n");
	printk("------------------------------------------------\n");

	/* Scenario 2: Producer-Consumer */
	producer = k_thread_create(&producer_data, producer_stack, STACKSIZE,
				   producer_thread, NULL, NULL, NULL,
				   PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(producer, "producer");

	consumer = k_thread_create(&consumer_data, consumer_stack, STACKSIZE,
				   consumer_thread, NULL, NULL, NULL,
				   PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(consumer, "consumer");

	/* Wait for producer-consumer to complete */
	k_msleep(200);

	printk("Scenario 2 completed\n\n");

	printk("Starting Scenario 3: Recursive Function Profiling\n");
	printk("--------------------------------------------------\n");

	/* Scenario 3: Recursive function profiling */
	compute_fibonacci();

	printk("Scenario 3 completed\n\n");

	printk("Starting Scenario 4: Variable Workload Simulation\n");
	printk("---------------------------------------------------\n");

	/* Scenario 4: Variable workload */
	worker = k_thread_create(&worker_data, worker_stack, STACKSIZE,
				 worker_thread, NULL, NULL, NULL,
				 PRIORITY + 1, 0, K_NO_WAIT);
	k_thread_name_set(worker, "worker");

	/* Wait for worker to complete */
	k_msleep(300);

	printk("Scenario 4 completed\n\n");

	printk("=================================================\n");
	printk("All demo scenarios completed!\n");
	printk("=================================================\n");

	return 0;
}
