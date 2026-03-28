/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026
 */

#ifndef ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_ID809_H_
#define ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_ID809_H_

#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/i2c.h>

#define ID809_READY_TOKEN 0xEE

#define ID809_CMD_PREFIX_CODE      0xAA55
#define ID809_RCM_PREFIX_CODE      0x55AA
#define ID809_CMD_DATA_PREFIX_CODE 0xA55A
#define ID809_RCM_DATA_PREFIX_CODE 0x5AA5

#define ID809_CMD_PACKET_TYPE  0xF0
#define ID809_DATA_PACKET_TYPE 0x0F

#define ID809_CMD_TEST_CONNECTION      0x0001
#define ID809_CMD_SET_PARAM            0x0002
#define ID809_CMD_GET_PARAM            0x0003
#define ID809_CMD_DEVICE_INFO          0x0004
#define ID809_CMD_ENTER_STANDBY_STATE  0x000C
#define ID809_CMD_GET_IMAGE            0x0020
#define ID809_CMD_FINGER_DETECT        0x0021
#define ID809_CMD_SLED_CTRL            0x0024
#define ID809_CMD_STORE_CHAR           0x0040
#define ID809_CMD_LOAD_CHAR            0x0041
#define ID809_CMD_UP_CHAR              0x0042
#define ID809_CMD_DOWN_CHAR            0x0043
#define ID809_CMD_DEL_CHAR             0x0044
#define ID809_CMD_GET_EMPTY_ID         0x0045
#define ID809_CMD_GET_STATUS           0x0046
#define ID809_CMD_GET_ENROLL_COUNT     0x0048
#define ID809_CMD_GET_ENROLLED_ID_LIST 0x0049
#define ID809_CMD_GENERATE             0x0060
#define ID809_CMD_MERGE                0x0061
#define ID809_CMD_SEARCH               0x0063
#define ID809_CMD_VERIFY               0x0064

#define ID809_ERR_SUCCESS          0x00
#define ID809_ERR_FAIL             0x01
#define ID809_ERR_VERIFY           0x10
#define ID809_ERR_IDENTIFY         0x11
#define ID809_ERR_TMPL_EMPTY       0x12
#define ID809_ERR_TMPL_NOT_EMPTY   0x13
#define ID809_ERR_ALL_TMPL_EMPTY   0x14
#define ID809_ERR_EMPTY_ID_NOEXIST 0x15
#define ID809_ERR_BROKEN_ID_NOEXIST 0x16
#define ID809_ERR_INVALID_TMPL_DATA 0x17
#define ID809_ERR_DUPLICATION_ID    0x18
#define ID809_ERR_BAD_QUALITY       0x19
#define ID809_ERR_MERGE_FAIL        0x1A
#define ID809_ERR_NOT_AUTHORIZED    0x1B
#define ID809_ERR_MEMORY            0x1C
#define ID809_ERR_INVALID_TMPL_NO   0x1D
#define ID809_ERR_INVALID_PARAM     0x22
#define ID809_ERR_TIMEOUT           0x23
#define ID809_ERR_GEN_COUNT         0x25
#define ID809_ERR_INVALID_BUFFER_ID 0x26
#define ID809_ERR_FP_NOT_DETECTED   0x28
#define ID809_ERR_FP_CANCEL         0x41
#define ID809_ERR_RECV_LENGTH       0x42
#define ID809_ERR_RECV_CKS          0x43
#define ID809_ERR_GATHER_OUT        0x45
#define ID809_ERR_RECV_TIMEOUT      0x46

#define ID809_PARAM_DEVICE_ID         0x00
#define ID809_PARAM_SECURITY_LEVEL    0x01
#define ID809_PARAM_DUPLICATION_CHECK 0x02
#define ID809_PARAM_BAUDRATE          0x03
#define ID809_PARAM_SELF_LEARN        0x04

#define ID809_CMD_HEADER_SIZE         8U
#define ID809_RCM_HEADER_SIZE         10U
#define ID809_CKS_SIZE                2U
#define ID809_CMD_PAYLOAD_SIZE        16U
#define ID809_FIXED_RESPONSE_DATA_SIZE 14U
#define ID809_TEMPLATE_PREFIX_SIZE    2U

#define ID809_ENROLL_SAMPLES 3U
#define ID809_I2C_CHUNK_SIZE 32U
#define ID809_I2C_POLL_MS    10U
#define ID809_READY_TIMEOUT_MS 2000U
#define ID809_RESPONSE_TIMEOUT_MS 5000U
#define ID809_MAX_TIMEOUT_MS  (3600U * 1000U)
#define ID809_MAX_TEMPLATE_SIZE 1008U
#define ID809_MAX_TEMPLATE_ID   255U

#define ID809_LED_MODE_BREATHING   1U
#define ID809_LED_MODE_FAST_BLINK  2U
#define ID809_LED_MODE_ON          3U
#define ID809_LED_MODE_OFF         4U

#define ID809_LED_COLOR_GREEN 1U
#define ID809_LED_COLOR_RED   2U
#define ID809_LED_COLOR_BLUE  4U

struct id809_config {
struct i2c_dt_spec bus;
uint16_t max_templates;
uint16_t template_size;
};

enum id809_enroll_state {
ID809_ENROLL_IDLE,
ID809_ENROLL_WAIT_SAMPLE_1,
ID809_ENROLL_WAIT_SAMPLE_2,
ID809_ENROLL_WAIT_SAMPLE_3,
ID809_ENROLL_READY,
};

struct id809_data {
struct k_mutex lock;
enum id809_enroll_state enroll_state;
uint16_t enroll_id;
int32_t match_threshold;
int32_t enroll_quality;
int32_t security_level;
uint32_t timeout_ms;
int32_t image_quality;
uint16_t last_match_id;
enum biometric_led_state led_state;
uint8_t packet_buf[ID809_MAX_TEMPLATE_SIZE + ID809_CMD_HEADER_SIZE +
   ID809_TEMPLATE_PREFIX_SIZE + ID809_CKS_SIZE];
};

#endif /* ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_ID809_H_ */
