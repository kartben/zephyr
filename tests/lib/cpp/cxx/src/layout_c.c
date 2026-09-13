/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Kernel object layout as the C compiler sees it, for the C++ side of this
 * test to compare against its own view. A type that lays out differently in
 * the two languages, such as one holding a struct that is empty in some
 * configuration, shows up as a mismatch here.
 */

#include <zephyr/kernel.h>

const size_t cxx_test_sizeof_spinlock = sizeof(struct k_spinlock);
const size_t cxx_test_sizeof_msgq = sizeof(struct k_msgq);
const size_t cxx_test_offsetof_msgq_msg_size = offsetof(struct k_msgq, msg_size);
const size_t cxx_test_sizeof_thread = sizeof(struct k_thread);
const size_t cxx_test_sizeof_cpu = sizeof(struct _cpu);
