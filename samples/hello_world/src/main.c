/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <math.h>

/**
 * west build -p auto -b stm32f401_mini -t rom_report samples/hello_world -- -DCONFIG_MINIMAL_LIBC=y
 * west build -p auto -b stm32f401_mini -t rom_report samples/hello_world -- -DCONFIG_NEWLIB_LIBC=y
 * west build -p auto -b stm32f401_mini -t rom_report samples/hello_world -- -DCONFIG_NEWLIB_LIBC_NANO=y
 * west build -p auto -b stm32f401_mini -t rom_report samples/hello_world -- -DCONFIG_PICOLIBC=y
 *
 */
int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD);
	printf("sqrt(2) = %f\n", sqrt(2.0));
	printf("sin(π) = %f\n", sin(3.141592));
	return 0;
}
