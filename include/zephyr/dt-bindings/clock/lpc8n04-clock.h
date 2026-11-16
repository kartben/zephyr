/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_LPC8N04_CLOCK_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_LPC8N04_CLOCK_H_

/**
 * @file
 * @brief NXP LPC8N04 peripheral clocks: bit numbers in SYSCON SYSAHBCLKCTRL.
 */

#define LPC8N04_CLK_RAM        2
#define LPC8N04_CLK_FLASH_REG  3
#define LPC8N04_CLK_FLASH      4
#define LPC8N04_CLK_I2C0       5
#define LPC8N04_CLK_GPIO       6
#define LPC8N04_CLK_SSP0       7
#define LPC8N04_CLK_CT16B0     8
#define LPC8N04_CLK_CT32B0     9
#define LPC8N04_CLK_RTC        10
#define LPC8N04_CLK_WDT        11
#define LPC8N04_CLK_TSENS      12
#define LPC8N04_CLK_IOCON      18
#define LPC8N04_CLK_EEPROM_REG 19
#define LPC8N04_CLK_EEPROM     20

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_LPC8N04_CLOCK_H_ */
