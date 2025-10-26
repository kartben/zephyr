/*
 * Alternative main.c demonstrating dtdoctor with forced errors.
 * 
 * To use this file:
 * 1. Rename this file to main.c (or swap with the default main.c)
 * 2. Use one of the overlay files from boards/ directory
 * 3. Build with: west build -b <board> -- -DZEPHYR_SCA_VARIANT=dtdoctor
 * 
 * This version unconditionally references the demo sensor, which will trigger
 * a build error that dtdoctor can help diagnose.
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

/* Devicetree node for LED */
#define LED0_NODE DT_ALIAS(led0)

/*
 * Devicetree node for a sensor that intentionally causes errors.
 * This unconditional reference will fail if the sensor is:
 * - Not defined in devicetree, OR
 * - Defined but disabled, OR
 * - Enabled but missing required driver Kconfig
 */
#define SENSOR_NODE DT_ALIAS(demo_sensor)

/* These will cause build errors that dtdoctor can help diagnose */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *const sensor = DEVICE_DT_GET(SENSOR_NODE);

int main(void)
{
	printf("DT Doctor Demo - Error Scenario\n");
	printf("================================\n\n");

	/* LED should work on most boards */
	if (!gpio_is_ready_dt(&led)) {
		printf("Error: LED GPIO is not ready\n");
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	printf("LED configured successfully\n");

	/* This will fail and dtdoctor will explain why */
	if (!device_is_ready(sensor)) {
		printf("Error: Demo sensor is not ready\n");
		printf("dtdoctor should have explained this error!\n");
		return -1;
	}

	printf("All devices ready - this shouldn't happen in error demo!\n");
	return 0;
}
