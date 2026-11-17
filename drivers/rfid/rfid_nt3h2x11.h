/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_RFID_RFID_NT3H2X11_H_
#define ZEPHYR_DRIVERS_RFID_RFID_NT3H2X11_H_

#include <zephyr/sys/util.h>

/* Memory map seen from I2C, in 16-byte blocks */
#define NT3H2X11_BLOCK_SIZE    16U
#define NT3H2X11_BLOCK_SRAM    0xF8U
#define NT3H2X11_SRAM_BLOCKS   4U
#define NT3H2X11_SRAM_SIZE     (NT3H2X11_SRAM_BLOCKS * NT3H2X11_BLOCK_SIZE)
#define NT3H2X11_BLOCK_SESSION 0xFEU

#define NT3H2X11_UID_SIZE 7U

/* Session registers in block 0xFE */
#define NT3H2X11_REG_NC 0x00U
#define NT3H2X11_REG_NS 0x06U

#define NT3H2X11_NC_NFCS_I2C_RST_ON_OFF BIT(7)
#define NT3H2X11_NC_PTHRU_ON_OFF        BIT(6)
#define NT3H2X11_NC_FD_OFF              GENMASK(5, 4)
#define NT3H2X11_NC_FD_ON               GENMASK(3, 2)
#define NT3H2X11_NC_SRAM_MIRROR_ON_OFF  BIT(1)
#define NT3H2X11_NC_TRANSFER_DIR        BIT(0) /* 1: NFC to I2C, 0: I2C to NFC */

/* FD_ON/FD_OFF = 00b: FD follows the NFC field */
#define NT3H2X11_NC_FD_FIELD 0U
/* FD_ON/FD_OFF = 11b: FD signals the pass-through handshake */
#define NT3H2X11_NC_FD_PTHRU                                                                       \
	(FIELD_PREP(NT3H2X11_NC_FD_OFF, 3U) | FIELD_PREP(NT3H2X11_NC_FD_ON, 3U))

#define NT3H2X11_NS_NDEF_DATA_READ   BIT(7)
#define NT3H2X11_NS_I2C_LOCKED       BIT(6)
#define NT3H2X11_NS_RF_LOCKED        BIT(5)
#define NT3H2X11_NS_SRAM_I2C_READY   BIT(4)
#define NT3H2X11_NS_SRAM_RF_READY    BIT(3)
#define NT3H2X11_NS_EEPROM_WR_ERR    BIT(2)
#define NT3H2X11_NS_EEPROM_WR_BUSY   BIT(1)
#define NT3H2X11_NS_RF_FIELD_PRESENT BIT(0)

#endif /* ZEPHYR_DRIVERS_RFID_RFID_NT3H2X11_H_ */
