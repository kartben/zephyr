/*
 * Copyright (c) 2024 Andrew Featherstone
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Peripheral reset identifiers for Raspberry Pi RP2350
 * @ingroup reset_controller_rp2350
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_RESET_RP2350_RESET_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_RESET_RP2350_RESET_H_

/**
 * @addtogroup reset_controller_raspberrypi Raspberry Pi reset controller helpers
 * @ingroup reset_controller_interface
 */

/**
 * @defgroup reset_controller_rp2350 Raspberry Pi RP2350 reset controller helpers
 * @brief Raspberry Pi RP2350 reset controller helpers
 * @ingroup reset_controller_raspberrypi
 *
 * @details Reset identifiers for use with the <tt>raspberrypi,pico-reset</tt> binding.
 * Identifiers follow the pattern @c RPI_PICO_RESETS_RESET_<PERIPHERAL>, where
 * @c <PERIPHERAL> is the RP2350 peripheral name (for example,
 * @c RPI_PICO_RESETS_RESET_I2C0 resets the I2C0 controller).
 * @{
 */

/** @cond INTERNAL_HIDDEN */

#define RPI_PICO_RESETS_RESET_ADC        0
#define RPI_PICO_RESETS_RESET_BUSCTRL    1
#define RPI_PICO_RESETS_RESET_DMA        2
#define RPI_PICO_RESETS_RESET_HSTX       3
#define RPI_PICO_RESETS_RESET_I2C0       4
#define RPI_PICO_RESETS_RESET_I2C1       5
#define RPI_PICO_RESETS_RESET_IO_BANK0   6
#define RPI_PICO_RESETS_RESET_IO_QSPI    7
#define RPI_PICO_RESETS_RESET_JTAG       8
#define RPI_PICO_RESETS_RESET_PADS_BANK0 9
#define RPI_PICO_RESETS_RESET_PADS_QSPI  10
#define RPI_PICO_RESETS_RESET_PIO0       11
#define RPI_PICO_RESETS_RESET_PIO1       12
#define RPI_PICO_RESETS_RESET_PIO2       13
#define RPI_PICO_RESETS_RESET_PLL_SYS    14
#define RPI_PICO_RESETS_RESET_PLL_USB    15
#define RPI_PICO_RESETS_RESET_PWM        16
#define RPI_PICO_RESETS_RESET_SHA256     17
#define RPI_PICO_RESETS_RESET_SPI0       18
#define RPI_PICO_RESETS_RESET_SPI1       19
#define RPI_PICO_RESETS_RESET_SYSCFG     20
#define RPI_PICO_RESETS_RESET_SYSINFO    21
#define RPI_PICO_RESETS_RESET_TBMAN      22
#define RPI_PICO_RESETS_RESET_TIMER0     23
#define RPI_PICO_RESETS_RESET_TIMER1     24
#define RPI_PICO_RESETS_RESET_TRNG       25
#define RPI_PICO_RESETS_RESET_UART0      26
#define RPI_PICO_RESETS_RESET_UART1      27
#define RPI_PICO_RESETS_RESET_USBCTRL    28

/** @endcond */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_RESET_RP2350_RESET_H_ */
