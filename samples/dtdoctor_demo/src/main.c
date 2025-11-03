/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

/* Devicetree node for LED (this should work on most boards) */
#define LED0_NODE DT_ALIAS(led0)

/*
 * Devicetree node for a sensor that may not exist or be disabled.
 * This demonstrates how dtdoctor helps diagnose devicetree issues.
 */
#define SENSOR_NODE DT_ALIAS(demo_sensor)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif

#if DT_NODE_HAS_STATUS(SENSOR_NODE, okay)
static const struct device *const sensor = DEVICE_DT_GET(SENSOR_NODE);
#endif

int main(void)
{
	printf("DT Doctor Demo Application\n");
	printf("===========================\n\n");

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
	printf("LED0 found and enabled\n");
	if (!gpio_is_ready_dt(&led)) {
		printf("Error: LED GPIO is not ready\n");
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	printf("LED configured successfully\n");
#else
	printf("LED0 not found or disabled - dtdoctor can help diagnose this!\n");
#endif

#if DT_NODE_HAS_STATUS(SENSOR_NODE, okay)
	printf("Demo sensor found and enabled\n");
	if (!device_is_ready(sensor)) {
		printf("Error: Demo sensor is not ready\n");
		printf("This might be due to missing Kconfig options.\n");
		printf("Use dtdoctor to diagnose!\n");
		return -1;
	}
	printf("Demo sensor ready\n");
#else
	printf("Demo sensor not found or disabled\n");
	printf("This is expected for this demo.\n");
#endif

	printf("\nApplication completed successfully\n");
	return 0;
}
