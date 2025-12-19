/*
 * Copyright (c) 2025 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/misc/escp_printer/escp_printer.h>

int main(void)
{
	const struct device *const printer = DEVICE_DT_GET(DT_NODELABEL(escp));

	printk("Epson ESC/P Printer Sample\n");

	if (!device_is_ready(printer)) {
		printk("Printer device not ready\n");
		return 0;
	}

	printk("Initializing printer...\n");
	escp_printer_init(printer);

	/* Print normal text */
	printk("Printing normal text\n");
	escp_printer_println(printer, "Hello from Zephyr!", 18);

	/* Print bold text */
	printk("Printing bold text\n");
	escp_printer_set_bold(printer, true);
	escp_printer_println(printer, "This is bold text", 17);
	escp_printer_set_bold(printer, false);

	/* Print italic text */
	printk("Printing italic text\n");
	escp_printer_set_italic(printer, true);
	escp_printer_println(printer, "This is italic text", 19);
	escp_printer_set_italic(printer, false);

	/* Print underlined text */
	printk("Printing underlined text\n");
	escp_printer_set_underline(printer, true);
	escp_printer_println(printer, "This is underlined text", 23);
	escp_printer_set_underline(printer, false);

	/* Add some blank lines */
	escp_printer_line_feed(printer);
	escp_printer_line_feed(printer);

	/* Final message */
	escp_printer_println(printer, "ESC/P Printer Demo Complete!", 28);

	/* Form feed to eject the page */
	escp_printer_form_feed(printer);

	printk("Printing complete!\n");

	return 0;
}
