#include <stdint.h>
#include <lvgl.h>

#include <zephyr/llext/symbol.h>

extern void printk(char *fmt, ...);

extern lv_obj_t *lv_label_create(lv_obj_t *parent);
extern void lv_label_set_text(lv_obj_t *obj, const char *text);

void fill_window(lv_obj_t *cont)
{
	lv_obj_t *label = lv_label_create(cont);
	lv_label_set_text(label,
			  "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed \n"
			  "do eiusmod tempor incididunt ut labore et dolore magna aliqua. \n"
			  "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris \n"
			  "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in \n"
			  "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla \n"
			  "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in.");
}

LL_EXTENSION_SYMBOL(fill_window);
