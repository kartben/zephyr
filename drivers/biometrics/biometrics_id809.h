/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Siratul Islam
 */

#ifndef ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_ID809_H_
#define ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_ID809_H_

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/biometrics.h>

/*
 * DFRobot ID809 capacitive fingerprint sensor protocol.
 *
 * All multi-byte fields are little-endian on the wire. The host always uses
 * SID=0 and DID=0 for default operation.
 *
 * Command packet (always 26 bytes):
 *   [0x55][0xAA][SID][DID][CMD_L][CMD_H][LEN_L][LEN_H]
 *   [payload, 16 bytes padded with zeros]
 *   [CKS_L][CKS_H]
 *
 * Response packet (always 26 bytes):
 *   [0xAA][0x55][SID][DID][RCM_L][RCM_H][LEN_L][LEN_H][RET_L][RET_H]
 *   [payload, 14 bytes padded with zeros]
 *   [CKS_L][CKS_H]
 *
 * Data response packet (variable length, 10 + LEN bytes total):
 *   [0xA5][0x5A][SID][DID][RCM_L][RCM_H][LEN_L][LEN_H][RET_L][RET_H]
 *   [data, LEN-2 bytes]
 *   [CKS_L][CKS_H]
 *
 * CKS = sum of bytes [2..total_len-3] stored little-endian.
 */

/* Packet prefix bytes (on-wire, little-endian representation) */
#define ID809_CMD_PREFIX_B0 0x55U  /* Command packet first byte  */
#define ID809_CMD_PREFIX_B1 0xAAU  /* Command packet second byte */
#define ID809_RCM_PREFIX_B0 0xAAU  /* ACK response first byte    */
#define ID809_RCM_PREFIX_B1 0x55U  /* ACK response second byte   */
#define ID809_DAT_PREFIX_B0 0xA5U  /* Data response first byte   */
#define ID809_DAT_PREFIX_B1 0x5AU  /* Data response second byte  */

/* Default device identifiers */
#define ID809_SID_HOST 0x00U
#define ID809_DID_DEV  0x00U

/* Packet layout constants */
#define ID809_HDR_SIZE     8U   /* PREFIX(2)+SID(1)+DID(1)+CMD/RCM(2)+LEN(2) */
#define ID809_EXTHDR_SIZE  10U  /* HDR + RET(2) */
#define ID809_CKS_SIZE     2U
#define ID809_CMD_PKT_SIZE 26U  /* Fixed command packet size */
#define ID809_ACK_PKT_SIZE 26U  /* Fixed ACK response packet size */
#define ID809_CMD_PAD_SIZE 16U  /* Padded payload area in command packet */
#define ID809_ACK_PAY_SIZE 14U  /* Payload area in ACK response (after RET) */

/* Buffer offsets */
#define ID809_OFF_PREFIX 0U
#define ID809_OFF_SID    2U
#define ID809_OFF_DID    3U
#define ID809_OFF_CMD    4U
#define ID809_OFF_LEN    6U
#define ID809_OFF_RET    8U   /* Only in response packets */
#define ID809_OFF_PAY    8U   /* Payload start in command packet */
#define ID809_OFF_RXPAY  10U  /* Payload start in response packet (after RET) */

/* Maximum buffer size: large enough for data responses */
#define ID809_MAX_PKT_SIZE 128U

/* Command codes (little-endian on wire) */
#define ID809_CMD_TEST_CONNECTION      0x0001U
#define ID809_CMD_SET_PARAM            0x0002U
#define ID809_CMD_GET_PARAM            0x0003U
#define ID809_CMD_DEVICE_INFO          0x0004U
#define ID809_CMD_GET_IMAGE            0x0020U
#define ID809_CMD_FINGER_DETECT        0x0021U
#define ID809_CMD_SLED_CTRL            0x0024U
#define ID809_CMD_STORE_CHAR           0x0040U
#define ID809_CMD_DEL_CHAR             0x0044U
#define ID809_CMD_GET_EMPTY_ID         0x0045U
#define ID809_CMD_GET_STATUS           0x0046U
#define ID809_CMD_GET_ENROLL_COUNT     0x0048U
#define ID809_CMD_GET_ENROLLED_ID_LIST 0x0049U
#define ID809_CMD_GENERATE             0x0060U
#define ID809_CMD_MERGE                0x0061U
#define ID809_CMD_SEARCH               0x0063U
#define ID809_CMD_VERIFY               0x0064U

/* Return codes */
#define ID809_RET_SUCCESS        0x0000U
#define ID809_RET_FAIL           0x0001U
#define ID809_RET_VERIFY_FAIL    0x0010U
#define ID809_RET_IDENTIFY_FAIL  0x0011U
#define ID809_RET_TMPL_EMPTY     0x0012U
#define ID809_RET_TMPL_NOT_EMPTY 0x0013U
#define ID809_RET_ALL_TMPL_EMPTY 0x0014U
#define ID809_RET_NO_EMPTY_ID    0x0015U
#define ID809_RET_DUPLICATION_ID 0x0018U
#define ID809_RET_BAD_QUALITY    0x0019U
#define ID809_RET_MERGE_FAIL     0x001AU
#define ID809_RET_NOT_AUTHORIZED 0x001BU
#define ID809_RET_MEMORY_ERR     0x001CU
#define ID809_RET_INVALID_TMPL   0x001DU
#define ID809_RET_INVALID_PARAM  0x0022U
#define ID809_RET_TIMEOUT        0x0023U
#define ID809_RET_FP_NOT_DETECT  0x0028U
#define ID809_RET_GATHER_OUT     0x0045U
#define ID809_RET_RECV_TIMEOUT   0x0046U

/* Parameter type codes for CMD_SET_PARAM / CMD_GET_PARAM */
#define ID809_PARAM_DEVICE_ID      0x00U
#define ID809_PARAM_SECURITY_LEVEL 0x01U
#define ID809_PARAM_DUP_CHECK      0x02U
#define ID809_PARAM_BAUDRATE       0x03U
#define ID809_PARAM_SELF_LEARN     0x04U

/* LED modes */
#define ID809_LED_BREATHING  0x01U
#define ID809_LED_FAST_BLINK 0x02U
#define ID809_LED_ON         0x03U
#define ID809_LED_OFF        0x04U
#define ID809_LED_SLOW_BLINK 0x07U

/* LED colors */
#define ID809_LED_COLOR_GREEN   0x01U
#define ID809_LED_COLOR_RED     0x02U
#define ID809_LED_COLOR_YELLOW  0x03U
#define ID809_LED_COLOR_BLUE    0x04U
#define ID809_LED_COLOR_CYAN    0x05U
#define ID809_LED_COLOR_MAGENTA 0x06U
#define ID809_LED_COLOR_WHITE   0x07U

/* RAM buffer IDs for fingerprint feature templates */
#define ID809_RAM_BUF_0 0x00U
#define ID809_RAM_BUF_1 0x01U
#define ID809_RAM_BUF_2 0x02U

/* Number of enrollment samples required by ID809 */
#define ID809_ENROLL_SAMPLES 3U

/* Default fingerprint library capacity */
#define ID809_DEFAULT_CAPACITY 80U

/* Polling interval for finger detect loop */
#define ID809_FINGER_POLL_MS 100U

/* UART packet timeout */
#define ID809_UART_TIMEOUT_MS 1000U

/* Maximum allowed operation timeout (1 hour) */
#define ID809_MAX_TIMEOUT_MS (3600U * 1000U)

/* Enrollment state */
enum id809_enroll_state {
	ID809_ENROLL_IDLE,
	ID809_ENROLL_SAMPLE_0,
	ID809_ENROLL_SAMPLE_1,
	ID809_ENROLL_SAMPLE_2,
	ID809_ENROLL_READY,
};

/* RX error codes */
enum id809_rx_error {
	ID809_RX_OK = 0,
	ID809_RX_OVERFLOW,
	ID809_RX_INVALID_LEN,
};

/* Packet buffer (shared for TX and RX) */
struct id809_packet {
	uint8_t buf[ID809_MAX_PKT_SIZE];
	volatile uint16_t len;
	volatile uint16_t offset; /* TX write position for ISR */
};

/* Driver data */
struct id809_data {
	const struct device *dev;

	struct k_mutex lock;
	struct k_spinlock irq_lock;

	struct k_sem uart_tx_sem;
	struct k_sem uart_rx_sem;

	struct id809_packet tx_pkt;
	struct id809_packet rx_pkt;
	volatile uint16_t rx_expected;
	volatile enum id809_rx_error rx_error;

	enum id809_enroll_state enroll_state;
	uint16_t enroll_id;

	uint16_t max_templates;
	uint16_t last_match_id;

	int32_t security_level;
	uint32_t timeout_ms;

	enum biometric_led_state led_state;
};

/* Driver configuration */
struct id809_config {
	const struct device *uart_dev;
	uint16_t max_templates;
};

#endif /* ZEPHYR_DRIVERS_BIOMETRICS_BIOMETRICS_ID809_H_ */
