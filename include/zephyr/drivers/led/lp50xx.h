/*
 * Copyright (c) 2020 Seagate Technology LLC
 * Copyright (c) 2022 Grinn
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for TI LP50xx LED driver channel definitions.
 * @ingroup led_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_LED_LP50XX_H_
#define ZEPHYR_INCLUDE_DRIVERS_LED_LP50XX_H_

/** Number of color channels per LED (red, green and blue). */
#define LP50XX_COLORS_PER_LED	3

/** Maximum number of LEDs supported by the LP5009. */
#define LP5009_MAX_LEDS		3
/** Maximum number of LEDs supported by the LP5012. */
#define LP5012_MAX_LEDS		4
/** Maximum number of LEDs supported by the LP5018. */
#define LP5018_MAX_LEDS		6
/** Maximum number of LEDs supported by the LP5024. */
#define LP5024_MAX_LEDS		8
/** Maximum number of LEDs supported by the LP5030. */
#define LP5030_MAX_LEDS		10
/** Maximum number of LEDs supported by the LP5036. */
#define LP5036_MAX_LEDS		12

/*
 * LED channels mapping.
 */

/* Bank channels */

/** Base channel number of the LED bank channels. */
#define LP50XX_BANK_CHAN_BASE		0
/** Channel number of the LED bank brightness. */
#define LP50XX_BANK_BRIGHT_CHAN		LP50XX_BANK_CHAN_BASE
/**
 * @brief Get the channel number of the LED bank color 1.
 *
 * @param led Unused.
 */
#define LP50XX_BANK_COL1_CHAN(led)	(LP50XX_BANK_CHAN_BASE + 1)
/**
 * @brief Get the channel number of the LED bank color 2.
 *
 * @param led Unused.
 */
#define LP50XX_BANK_COL2_CHAN(led)	(LP50XX_BANK_CHAN_BASE + 2)
/**
 * @brief Get the channel number of the LED bank color 3.
 *
 * @param led Unused.
 */
#define LP50XX_BANK_COL3_CHAN(led)	(LP50XX_BANK_CHAN_BASE + 3)

/* LED brightness channels. */

/** Base channel number of the LED brightness channels. */
#define LP50XX_LED_BRIGHT_CHAN_BASE	4
/**
 * @brief Get the brightness channel number of a LED.
 *
 * @param led LED number.
 */
#define LP50XX_LED_BRIGHT_CHAN(led)	(LP50XX_LED_BRIGHT_CHAN_BASE + led)

/*
 * LED color channels.
 *
 * Each channel definition is compatible with the following chips:
 *   - LP5012_XXX => LP5009 / LP5012
 *   - LP5024_XXX => LP5018 / LP5024
 *   - LP5036_XXX => LP5030 / LP5036
 */

/** Base channel number of the LED color channels (LP5009/LP5012). */
#define LP5012_LED_COL_CHAN_BASE	8
/**
 * @brief Get the channel number of color 1 of a LED (LP5009/LP5012).
 *
 * @param led LED number.
 */
#define LP5012_LED_COL1_CHAN(led) \
	(LP5012_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED)
/**
 * @brief Get the channel number of color 2 of a LED (LP5009/LP5012).
 *
 * @param led LED number.
 */
#define LP5012_LED_COL2_CHAN(led) \
	(LP5012_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED + 1)
/**
 * @brief Get the channel number of color 3 of a LED (LP5009/LP5012).
 *
 * @param led LED number.
 */
#define LP5012_LED_COL3_CHAN(led) \
	(LP5012_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED + 2)

/** Base channel number of the LED color channels (LP5018/LP5024). */
#define LP5024_LED_COL_CHAN_BASE	12
/**
 * @brief Get the channel number of color 1 of a LED (LP5018/LP5024).
 *
 * @param led LED number.
 */
#define LP5024_LED_COL1_CHAN(led) \
	(LP5024_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED)
/**
 * @brief Get the channel number of color 2 of a LED (LP5018/LP5024).
 *
 * @param led LED number.
 */
#define LP5024_LED_COL2_CHAN(led) \
	(LP5024_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED + 1)
/**
 * @brief Get the channel number of color 3 of a LED (LP5018/LP5024).
 *
 * @param led LED number.
 */
#define LP5024_LED_COL3_CHAN(led) \
	(LP5024_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED + 2)

/** Base channel number of the LED color channels (LP5030/LP5036). */
#define LP5036_LED_COL_CHAN_BASE	16
/**
 * @brief Get the channel number of color 1 of a LED (LP5030/LP5036).
 *
 * @param led LED number.
 */
#define LP5036_LED_COL1_CHAN(led) \
	(LP5036_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED)
/**
 * @brief Get the channel number of color 2 of a LED (LP5030/LP5036).
 *
 * @param led LED number.
 */
#define LP5036_LED_COL2_CHAN(led) \
	(LP5036_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED + 1)
/**
 * @brief Get the channel number of color 3 of a LED (LP5030/LP5036).
 *
 * @param led LED number.
 */
#define LP5036_LED_COL3_CHAN(led) \
	(LP5036_LED_COL_CHAN_BASE + led * LP50XX_COLORS_PER_LED + 2)

#endif /* ZEPHYR_INCLUDE_DRIVERS_LED_LP50XX_H_ */
