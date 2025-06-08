/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <errno.h>

/**
 * @addtogroup sys_util_tests
 * @{
 */

/**
 * @brief Test wait_for works as expected with typical use cases
 *
 * @see WAIT_FOR()
 */

ZTEST(sys_util, test_wait_for)
{
	uint32_t start, end, expected;

	zassert_true(WAIT_FOR(true, 0, NULL), "true, no wait, NULL");
	zassert_true(WAIT_FOR(true, 0, k_yield()), "true, no wait, yield");
	zassert_false(WAIT_FOR(false, 0, k_yield()), "false, no wait, yield");
	zassert_true(WAIT_FOR(true, 1, k_yield()), "true, 1usec, yield");
	zassert_false(WAIT_FOR(false, 1, k_yield()), "false, 1usec, yield");
	zassert_true(WAIT_FOR(true, 1000, k_yield()), "true, 1msec, yield");


	expected = 1000*(sys_clock_hw_cycles_per_sec()/USEC_PER_SEC);
	start = k_cycle_get_32();
	zassert_false(WAIT_FOR(false, 1000, k_yield()), "true, 1msec, yield");
	end = k_cycle_get_32();
	zassert_true(end-start >= expected, "wait for 1ms");
}

/**
 * @brief Test NUM_VA_ARGS works as expected with typical use cases
 *
 * @see NUM_VA_ARGS()
 */

ZTEST(sys_util, test_NUM_VA_ARGS)
{
	zassert_equal(0, NUM_VA_ARGS());
	zassert_equal(1, NUM_VA_ARGS(_1));
	zassert_equal(2, NUM_VA_ARGS(_1, _2));
	/* support up to 63 args */
	zassert_equal(63, NUM_VA_ARGS(LISTIFY(63, ~, (,))));
}

/**
 * @brief Test NUM_VA_ARGS_LESS_1 works as expected with typical use cases
 *
 * @see NUM_VA_ARGS_LESS_1()
 */

ZTEST(sys_util, test_NUM_VA_ARGS_LESS_1)
{
	zassert_equal(0, NUM_VA_ARGS_LESS_1());
	zassert_equal(0, NUM_VA_ARGS_LESS_1(_1));
	zassert_equal(1, NUM_VA_ARGS_LESS_1(_1, _2));
	/* support up to 64 args */
        zassert_equal(63, NUM_VA_ARGS_LESS_1(LISTIFY(64, ~, (,))));
}

ZTEST(sys_util, test_clamp_inrange)
{
       zassert_equal(CLAMP(5, 0, 10), 5);
       zassert_equal(CLAMP(-1, 0, 10), 0);
       zassert_equal(CLAMP(20, 0, 10), 10);

       zassert_true(IN_RANGE(5, 0, 10));
       zassert_false(IN_RANGE(-1, 0, 10));
       zassert_false(IN_RANGE(11, 0, 10));
}

ZTEST(sys_util, test_log2ceil_nhpot)
{
       zassert_equal(LOG2CEIL(1), 0);
       zassert_equal(LOG2CEIL(2), 1);
       zassert_equal(LOG2CEIL(3), 2);
       zassert_equal(LOG2CEIL(8), 3);

       zassert_equal(NHPOT(1), 1U);
       zassert_equal(NHPOT(5), 8U);
       zassert_equal(NHPOT(0), 1U);
       zassert_equal(NHPOT((1ULL << 63) + 1), 0ULL);
}

ZTEST(sys_util, test_hex_conversions)
{
       uint8_t val;
       char c;

       zassert_ok(char2hex('a', &val));
       zassert_equal(val, 10);
       zassert_ok(hex2char(10, &c));
       zassert_equal(c, 'a');

       zassert_equal(char2hex('z', &val), -EINVAL);
       zassert_equal(hex2char(20, &c), -EINVAL);

       const uint8_t in[] = {0xde, 0xad, 0xbe, 0xef};
       char hex[9];
       uint8_t out[4];
       size_t len;

       len = bin2hex(in, sizeof(in), hex, sizeof(hex));
       zassert_equal(len, 8);
       zassert_mem_equal(hex, "deadbeef", len);

       zassert_equal(hex2bin(hex, len, out, sizeof(out)), sizeof(out));
       zassert_mem_equal(out, in, sizeof(in));
}
/**
 * @}
 */


/**
 * @defgroup sys_util_tests Sys Util Tests
 * @ingroup all_tests
 * @{
 * @}
 */

ZTEST_SUITE(sys_util, NULL, NULL, NULL, NULL, NULL);
