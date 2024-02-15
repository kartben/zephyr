#include <stdint.h>
#include <lvgl.h>

#include <zephyr/llext/symbol.h>

extern void printk(char *fmt, ...);

extern lv_obj_t *lv_bar_create(lv_obj_t *parent);
extern void lv_obj_set_size(struct _lv_obj_t *obj, lv_coord_t w, lv_coord_t h);
extern void lv_obj_center(struct _lv_obj_t *obj);
extern void lv_anim_init(lv_anim_t *a);
extern void lv_bar_set_value(lv_obj_t *obj, int32_t value, lv_anim_enable_t anim);
extern int32_t lv_anim_path_ease_in(const lv_anim_t *a);
extern lv_anim_t *lv_anim_start(const lv_anim_t *a);

void fill_window(lv_obj_t *cont)
{
	lv_obj_t *bar1 = lv_bar_create(cont);
	lv_obj_set_size(bar1, 200, 20);
	lv_obj_center(bar1);

	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_bar_set_value);
	lv_anim_set_values(&a, 0, 100);
	lv_anim_set_time(&a, 5000);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
	lv_anim_set_var(&a, bar1);
	lv_anim_start(&a);
}

LL_EXTENSION_SYMBOL(fill_window);
