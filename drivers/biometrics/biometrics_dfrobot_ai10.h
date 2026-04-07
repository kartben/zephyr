/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_DFROBOT_AI10_H_
#define ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_DFROBOT_AI10_H_

#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/uart.h>

#define AI10_SYNC_H 0xEF
#define AI10_SYNC_L 0xAA

#define AI10_MID_RELAY 0x00
#define AI10_MID_NOTE  0x01

#define AI10_CMD_RESET           0x10
#define AI10_CMD_VERIFY          0x12
#define AI10_CMD_ENROLL_SINGLE   0x1D
#define AI10_CMD_DEL_USER        0x20
#define AI10_CMD_DEL_ALL_USER    0x21
#define AI10_CMD_GET_ALL_USERID  0x24
#define AI10_CMD_GET_USER_INFO   0x22

#define AI10_FACE_DIR_UNDEF 0x00

#define AI10_ADMIN_NORMAL 0x00
#define AI10_ADMIN_ADMIN  0x01

#define AI10_RESULT_SUCCESS           0x00
#define AI10_RESULT_FAILED_UNKNOWN_USER 0x08
#define AI10_RESULT_FAILED_MAX_USER   0x09
#define AI10_RESULT_FAILED_FACE_ENROLLED 0x0A
#define AI10_RESULT_FAILED_CAMERA     0x04
#define AI10_RESULT_FAILED_TIMEOUT    0x0D

#define AI10_ENROLL_DATA_SIZE 35U

#define AI10_MAX_PAYLOAD 256U
#define AI10_MAX_FRAME   (2U + 3U + AI10_MAX_PAYLOAD + 1U)

#define AI10_UART_CHUNK_TIMEOUT_MS 500U

enum ai10_rx_error {
	AI10_RX_OK = 0,
	AI10_RX_OVERFLOW,
	AI10_RX_BAD_LEN,
};

struct ai10_packet {
	uint8_t buf[AI10_MAX_FRAME];
	volatile uint16_t len;
	volatile uint16_t offset;
};

struct ai10_data {
	const struct device *dev;

	struct k_mutex lock;
	struct k_spinlock irq_lock;

	struct k_sem uart_tx_sem;
	struct k_sem uart_rx_sem;

	struct ai10_packet tx_pkt;
	struct ai10_packet rx_pkt;
	volatile uint16_t rx_expected;
	volatile enum ai10_rx_error rx_error;

	bool enroll_active;
	uint16_t enroll_template_id;

	bool enroll_opts_valid;
	bool enroll_is_admin;
	char enroll_custom_name[32];

	uint32_t timeout_ms;
	int32_t last_image_quality;
};

struct ai10_config {
	const struct device *uart_dev;
};

#endif /* ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_DFROBOT_AI10_H_ */
