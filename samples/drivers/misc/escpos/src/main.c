/*
 * Copyright (c) 2025 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/misc/escpos/escpos.h>

#define PRINT_STR(printer, str) escpos_println(printer, str, strlen(str))

int main(void)
{
	const struct device *const printer = DEVICE_DT_GET(DT_NODELABEL(escpos));

	printk("ESC/POS Thermal Printer Sample\n");

	if (!device_is_ready(printer)) {
		printk("Printer device not ready\n");
		return 0;
	}

	printk("Initializing printer...\n");
	escpos_init(printer);

	/* Print normal text */
	printk("Printing normal text\n");
	PRINT_STR(printer, "Hello from Zephyr!");

	/* Print bold text */
	printk("Printing bold text\n");
	escpos_set_bold(printer, true);
	PRINT_STR(printer, "This is bold text");
	escpos_set_bold(printer, false);

	/* Print underlined text */
	printk("Printing underlined text\n");
	escpos_set_underline(printer, true);
	PRINT_STR(printer, "This is underlined text");
	escpos_set_underline(printer, false);

	/* Add some blank lines */
	escpos_line_feed(printer);
	escpos_line_feed(printer);

	/* Final message */
	PRINT_STR(printer, "ESC/POS Printer Demo Complete!");

	/* Feed and cut the paper */
	escpos_line_feed(printer);
	escpos_line_feed(printer);
	escpos_line_feed(printer);
	escpos_cut_paper(printer);

	printk("Printing complete!\n");

	return 0;
}
