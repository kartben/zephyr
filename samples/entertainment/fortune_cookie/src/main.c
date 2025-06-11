/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>
#include <stdio.h>

static const char *fortunes[] = {
    "You will find unexpected adventure.",
    "A thrilling time is in your near future.",
    "Keep it simple and take a chance.",
    "Today is a great day for innovation.",
    "Fortune favors the bold."
};

int main(void)
{
    uint32_t choice = sys_rand32_get() % ARRAY_SIZE(fortunes);

    printf("Fortune: %s\n", fortunes[choice]);

    return 0;
}
