/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_RFID_RFID_NT3H2X11_EMUL_H_
#define ZEPHYR_DRIVERS_RFID_RFID_NT3H2X11_EMUL_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/emul.h>

/**
 * @brief Turn the reader's RF field on or off.
 *
 * Turning the field off disables pass-through mode, as on the real tag.
 *
 * @param target NT3H2x11 emulator.
 * @param on True when a reader is in the field.
 */
void nt3h2x11_emul_set_field(const struct emul *target, bool on);

/**
 * @brief Write the whole SRAM from the reader side.
 *
 * @param target NT3H2x11 emulator.
 * @param buf 64 bytes written by the reader.
 *
 * @retval 0 Data is ready for the host.
 * @retval -EAGAIN Pass-through is not enabled from NFC to I2C, or the host has
 *         not read the previous data yet.
 */
int nt3h2x11_emul_reader_write(const struct emul *target, const uint8_t *buf);

/**
 * @brief Read the whole SRAM from the reader side.
 *
 * @param target NT3H2x11 emulator.
 * @param buf 64-byte buffer for the data written by the host.
 *
 * @retval 0 Data read; the host may write again.
 * @retval -EAGAIN Pass-through is not enabled from I2C to NFC, or the host has
 *         not written any data yet.
 */
int nt3h2x11_emul_reader_read(const struct emul *target, uint8_t *buf);

#endif /* ZEPHYR_DRIVERS_RFID_RFID_NT3H2X11_EMUL_H_ */
