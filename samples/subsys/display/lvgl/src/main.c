/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/random/rand32.h>

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "zephyr_m5stickcplusR.h"


#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/hrs.h>


#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

static lv_obj_t * chart1;
static lv_chart_series_t * ser_x;
static lv_chart_series_t * ser_y;
static lv_chart_series_t * ser_z;

const struct device *display_dev;
const struct device *sensor;


static const struct i2c_dt_spec i2c_axp192 =
	I2C_DT_SPEC_GET(DT_NODELABEL(axp192));



/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
 */
#define SW0_NODE	DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;



/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
 */
#define SW1_NODE	DT_ALIAS(sw1)
#if !DT_NODE_HAS_STATUS(SW1_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button2 = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios,
							      {0});
static struct gpio_callback button2_cb_data;





uint8_t new_active_tab=0;
#define MAX_TABS 3

void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	new_active_tab = (new_active_tab + 1) % MAX_TABS ;
}


uint8_t backlight_on = 0;

void button2_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	printk("Button2 pressed at %" PRIu32 "\n", k_cycle_get_32());
	backlight_on = !backlight_on;
}


lv_obj_t *tabview;
lv_timer_t *fetch_and_display_timer;





static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL))
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(void)
{
	int err;

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

int batVoltageToPercentage(float volt) {
  if (volt < 3.20) return -1;
  if (volt < 3.27) return 0;
  if (volt < 3.61) return 5;
  if (volt < 3.69) return 10;
  if (volt < 3.71) return 15;
  if (volt < 3.73) return 20;
  if (volt < 3.75) return 25;
  if (volt < 3.77) return 30;
  if (volt < 3.79) return 35;
  if (volt < 3.80) return 40;
  if (volt < 3.82) return 45;
  if (volt < 3.84) return 50;
  if (volt < 3.85) return 55;
  if (volt < 3.87) return 60;
  if (volt < 3.91) return 65;
  if (volt < 3.95) return 70;
  if (volt < 3.98) return 75;
  if (volt < 4.02) return 80;
  if (volt < 4.08) return 85;
  if (volt < 4.11) return 90;
  if (volt < 4.15) return 95;
  if (volt < 4.20) return 100;
  if (volt >= 4.20) return 100;

}

uint16_t Read13Bit(uint8_t addr) {
    uint16_t data = 0;
    uint8_t buf[2];
	i2c_burst_read_dt(&i2c_axp192, addr, buf, 2);
    data = ((buf[0] << 5) + buf[1]);  //
    return data;
}

uint32_t read24bit(uint8_t addr) {
    uint32_t data = 0;
    uint8_t buf[4];
	i2c_burst_read_dt(&i2c_axp192, addr, buf, 4);
    for (int i = 0; i < 3; i++) {
        data <<= 8;
        data |= buf[i];
    }
    return data;
}



float getBatPower() {
    float VoltageLSB = 1.1;
    float CurrentLCS = 0.5;
    uint32_t ReData  = read24bit(0x70);
    return VoltageLSB * CurrentLCS * ReData / 1000.0;
}



float getBatCurrent() {
    float ADCLSB        = 0.5;
    uint16_t currentIn  = Read13Bit(0x7A);
    uint16_t currentOut = Read13Bit(0x7C);
    return (currentIn - currentOut) * ADCLSB;
}

float getBatVoltage() {
	int reg = 0x78;
    uint16_t data = 0;
    uint8_t buf[2];

	i2c_burst_read_dt(&i2c_axp192, 0x78, buf, 2);

    data = ((buf[0] << 4) + buf[1]); 
    float ADCLSB    = 1.1 / 1000.0;
	float battery_voltage = data * ADCLSB;

	return battery_voltage;
}

static void bas_notify(void)
{
	float battery_voltage = getBatVoltage();

	bt_bas_set_battery_level(batVoltageToPercentage(battery_voltage));
}

static void hrs_notify(void)
{
	bt_hrs_notify(80 + (sys_rand32_get() % 10));
}


static void fetch_and_display(lv_timer_t * timer)
{
	static unsigned int count;
	struct sensor_value accel[3];
	const char *overrun = "";
	int rc = sensor_sample_fetch(sensor);

	++count;
	if (rc == -EBADMSG) {
		/* Sample overrun.  Ignore in polled mode. */
		if (IS_ENABLED(CONFIG_LIS2DH_TRIGGER)) {
			overrun = "[OVERRUN] ";
		}
		rc = 0;
	}
	if (rc == 0) {
		rc = sensor_channel_get(sensor,
					SENSOR_CHAN_ACCEL_XYZ,
					accel);
	}
	if (rc < 0) {
		LOG_ERR("ERROR: Update failed: %d\n", rc);
	} else {
	    lv_chart_set_next_value(chart1, ser_x, sensor_value_to_double(&accel[0]));
	    lv_chart_set_next_value(chart1, ser_y, sensor_value_to_double(&accel[1]));
	    lv_chart_set_next_value(chart1, ser_z, sensor_value_to_double(&accel[2]));
	}
}

void accelerometer_chart(lv_obj_t * parent)
{
    chart1 = lv_chart_create(parent);
    lv_obj_set_size(chart1, 135, 240);
    lv_obj_center(chart1);
    lv_chart_set_type(chart1, LV_CHART_TYPE_LINE);

    lv_chart_set_div_line_count(chart1, 5, 8);

	lv_chart_set_range(chart1, LV_CHART_AXIS_PRIMARY_Y, -20, 20);

    lv_chart_set_update_mode(chart1, LV_CHART_UPDATE_MODE_SHIFT);

    ser_x = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ser_y = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    ser_z = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);

	lv_chart_set_point_count(chart1, 50);
    /*Do not display points on the data*/
    lv_obj_set_style_size(chart1, 0, LV_PART_INDICATOR);

    fetch_and_display_timer = lv_timer_create(fetch_and_display, 20, NULL);
	lv_timer_pause(fetch_and_display_timer);
}  

LV_IMG_DECLARE(zephyr_m5stickcplusR);

void zephyr_logo(lv_obj_t * parent) {

	lv_obj_t * img1 = lv_img_create(parent);
	lv_img_set_src(img1, &zephyr_m5stickcplusR);
	//lv_obj_align(img1, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);
}

lv_obj_t *battery_mw_label;;
lv_obj_t *battery_voltage_label;;

static void refresh_battery_info(lv_timer_t * timer) {
	char buf[64];
	sprintf(buf, "%0.02f mA", getBatCurrent());
	lv_label_set_text(battery_mw_label, buf);

	sprintf(buf, "%0.02f V", getBatVoltage());
	lv_label_set_text(battery_voltage_label, buf);
}

void battery_info(lv_obj_t * parent) {
	battery_mw_label = lv_label_create(parent);
	battery_voltage_label = lv_label_create(parent);

	lv_obj_align(battery_mw_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_align(battery_voltage_label, LV_ALIGN_BOTTOM_MID, 0, 0);

	lv_timer_create(refresh_battery_info, 5000, NULL);
}

void main(void)
{
	int err, ret;




	if (!device_is_ready(i2c_axp192.bus)) {
		printk("AXP192 I2C bus not ready.\n");
		return;
	}

	i2c_reg_write_byte_dt(&i2c_axp192, 0x28, 0xCC);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x82, 0xff);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x33, 0xC0);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x12, 0x5F);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x36, 0x0c);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x91, 0xf0);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x90, 0x02);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x30, 0x80);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x39, 0xfc);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x35, 0xa2);
	i2c_reg_write_byte_dt(&i2c_axp192, 0x32, 0x46);




	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);



	if (!gpio_is_ready_dt(&button2)) {
		printk("Error: button device %s is not ready\n",
		       button2.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&button2, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button2.port->name, button2.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button2,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button2.port->name, button2.pin);
		return;
	}

	gpio_init_callback(&button2_cb_data, button2_pressed, BIT(button2.pin));
	gpio_add_callback(button2.port, &button2_cb_data);
	printk("Set up button at %s pin %d\n", button2.port->name, button2.pin);



	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_ready();

	bt_conn_auth_cb_register(&auth_cb_display);


	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return;
	}

	sensor = DEVICE_DT_GET_ANY(invensense_mpu6050);
	if (sensor == NULL) {
		LOG_ERR("No lis2dh/lis3dh device found\n");
		return;
	}
	if (!device_is_ready(sensor)) {
		LOG_ERR("Device %s is not ready\n", sensor->name);
		return;
	}

    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 0);

    lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Tab 1");
    lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Tab 2");
    lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Tab 3");

    lv_obj_set_style_pad_all(tabview, 0, 0);
    lv_obj_set_style_pad_all(tab1, 0, 0);
    lv_obj_set_style_pad_all(tab2, 0, 0);


	lv_obj_t *tab_buttons = lv_tabview_get_tab_btns(tabview);
	lv_btnmatrix_set_btn_ctrl(tab_buttons, 0, LV_BTNMATRIX_CTRL_HIDDEN);
	lv_btnmatrix_set_btn_ctrl(tab_buttons, 1, LV_BTNMATRIX_CTRL_HIDDEN);


	zephyr_logo(tab1);
	accelerometer_chart(tab2);
	battery_info(tab3);

	display_blanking_off(display_dev);

	static int i = 0 ;

	while (1) {
		lv_task_handler();
		k_sleep(K_MSEC(10));

		// set proper back_light
		if (backlight_on) {
			i2c_reg_write_byte_dt(&i2c_axp192, 0x28, 0xCC);
		} else {
//			i2c_reg_write_byte_dt(&i2c_axp192, 0x28, 0x7C);
			i2c_reg_write_byte_dt(&i2c_axp192, 0x28, 0x0C);
		}
			



		if (new_active_tab != lv_tabview_get_tab_act(tabview)) {

			if(new_active_tab == 1) {
				printk("resume timer\n");
				lv_timer_resume(fetch_and_display_timer);
			} else {
				printk("pause timer\n");
				lv_timer_pause(fetch_and_display_timer);
			}


			lv_tabview_set_act(tabview, new_active_tab, LV_ANIM_ON);
		}

		if (i++ % 100 == 0) {
			//printk(active_tab);
			hrs_notify();
			bas_notify();

			// print lv_tabview_get_tab_act
			printk("Tabview active tab: %d\n", lv_tabview_get_tab_act(tabview));

			// log energy consumption
			printk("Battery current: %0.02f\n", getBatCurrent());
			printk("Battery voltage: %0.02f\n", getBatVoltage());
			printk("Battery power: %0.02f\n", getBatPower());

		}
	}
}