/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup dshot_interface
 * @brief Main header file for the DShot electronic speed controller API.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DSHOT_H_
#define ZEPHYR_INCLUDE_DRIVERS_DSHOT_H_

/**
 * @brief Interfaces for DShot electronic speed controllers.
 * @defgroup dshot_interface DShot
 * @since 4.6
 * @version 0.1.0
 * @ingroup io_interfaces
 * @{
 *
 * DShot is the digital protocol flight controllers use to command brushless
 * motor electronic speed controllers (ESC). Each frame carries an 11-bit
 * value, a telemetry request bit and a 4-bit checksum, sent MSB first on
 * one signal line per motor.
 *
 * A DShot device drives a fixed set of channels, one per ESC. The
 * application stages a value for each channel with @ref dshot_set_throttle
 * and transmits all staged frames with @ref dshot_send, typically from its
 * control loop.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of bits in a DShot frame. */
#define DSHOT_FRAME_BITS 16U

/** Largest value of the 11-bit frame payload. */
#define DSHOT_VALUE_MAX 2047U

/** Payload value that stops the motor. */
#define DSHOT_THROTTLE_STOP 0U

/** Smallest throttle value; payload values 1 to 47 are commands. */
#define DSHOT_THROTTLE_MIN 48U

/** Largest throttle value. */
#define DSHOT_THROTTLE_MAX DSHOT_VALUE_MAX

/** @brief DShot bit rates, in kbit/s. */
enum dshot_speed {
	/** DShot150, 6.667 us bit period. */
	DSHOT_SPEED_150 = 150,
	/** DShot300, 3.333 us bit period. */
	DSHOT_SPEED_300 = 300,
	/** DShot600, 1.667 us bit period. */
	DSHOT_SPEED_600 = 600,
	/** DShot1200, 0.833 us bit period. */
	DSHOT_SPEED_1200 = 1200,
};

/**
 * @brief DShot special commands.
 *
 * Commands occupy payload values 0 to 47. ESCs only accept them while the
 * motor is stopped. Support for the individual commands depends on the ESC
 * firmware.
 */
enum dshot_command {
	/** Stop the motor. */
	DSHOT_CMD_MOTOR_STOP = 0,
	/** Beep with tone 1. */
	DSHOT_CMD_BEACON1 = 1,
	/** Beep with tone 2. */
	DSHOT_CMD_BEACON2 = 2,
	/** Beep with tone 3. */
	DSHOT_CMD_BEACON3 = 3,
	/** Beep with tone 4. */
	DSHOT_CMD_BEACON4 = 4,
	/** Beep with tone 5. */
	DSHOT_CMD_BEACON5 = 5,
	/** Request ESC information on the telemetry line. */
	DSHOT_CMD_ESC_INFO = 6,
	/** Set spin direction 1. */
	DSHOT_CMD_SPIN_DIRECTION_1 = 7,
	/** Set spin direction 2. */
	DSHOT_CMD_SPIN_DIRECTION_2 = 8,
	/** Disable 3D (bidirectional rotation) mode. */
	DSHOT_CMD_3D_MODE_OFF = 9,
	/** Enable 3D (bidirectional rotation) mode. */
	DSHOT_CMD_3D_MODE_ON = 10,
	/** Request the ESC settings. */
	DSHOT_CMD_SETTINGS_REQUEST = 11,
	/** Save the current settings to the ESC non-volatile memory. */
	DSHOT_CMD_SAVE_SETTINGS = 12,
	/** Enable extended DShot telemetry. */
	DSHOT_CMD_EXTENDED_TELEMETRY_ENABLE = 13,
	/** Disable extended DShot telemetry. */
	DSHOT_CMD_EXTENDED_TELEMETRY_DISABLE = 14,
	/** Set the normal spin direction. */
	DSHOT_CMD_SPIN_DIRECTION_NORMAL = 20,
	/** Set the reversed spin direction. */
	DSHOT_CMD_SPIN_DIRECTION_REVERSED = 21,
	/** Turn ESC LED 0 on. */
	DSHOT_CMD_LED0_ON = 22,
	/** Turn ESC LED 1 on. */
	DSHOT_CMD_LED1_ON = 23,
	/** Turn ESC LED 2 on. */
	DSHOT_CMD_LED2_ON = 24,
	/** Turn ESC LED 3 on. */
	DSHOT_CMD_LED3_ON = 25,
	/** Turn ESC LED 0 off. */
	DSHOT_CMD_LED0_OFF = 26,
	/** Turn ESC LED 1 off. */
	DSHOT_CMD_LED1_OFF = 27,
	/** Turn ESC LED 2 off. */
	DSHOT_CMD_LED2_OFF = 28,
	/** Turn ESC LED 3 off. */
	DSHOT_CMD_LED3_OFF = 29,
	/** Toggle the audio stream mode. */
	DSHOT_CMD_AUDIO_STREAM_MODE_ON_OFF = 30,
	/** Toggle the silent mode. */
	DSHOT_CMD_SILENT_MODE_ON_OFF = 31,
	/** Disable telemetry on the signal line. */
	DSHOT_CMD_SIGNAL_LINE_TELEMETRY_DISABLE = 32,
	/** Enable telemetry on the signal line. */
	DSHOT_CMD_SIGNAL_LINE_TELEMETRY_ENABLE = 33,
	/** Send eRPM telemetry continuously on the signal line. */
	DSHOT_CMD_SIGNAL_LINE_CONTINUOUS_ERPM_TELEMETRY = 34,
	/** Send eRPM period telemetry continuously on the signal line. */
	DSHOT_CMD_SIGNAL_LINE_CONTINUOUS_ERPM_PERIOD_TELEMETRY = 35,
	/** Request a temperature telemetry frame. */
	DSHOT_CMD_SIGNAL_LINE_TEMPERATURE_TELEMETRY = 42,
	/** Request a voltage telemetry frame. */
	DSHOT_CMD_SIGNAL_LINE_VOLTAGE_TELEMETRY = 43,
	/** Request a current telemetry frame. */
	DSHOT_CMD_SIGNAL_LINE_CURRENT_TELEMETRY = 44,
	/** Request a consumption telemetry frame. */
	DSHOT_CMD_SIGNAL_LINE_CONSUMPTION_TELEMETRY = 45,
	/** Request an eRPM telemetry frame. */
	DSHOT_CMD_SIGNAL_LINE_ERPM_TELEMETRY = 46,
	/** Request an eRPM period telemetry frame. */
	DSHOT_CMD_SIGNAL_LINE_ERPM_PERIOD_TELEMETRY = 47,
};

/** Largest command value. */
#define DSHOT_CMD_MAX DSHOT_CMD_SIGNAL_LINE_ERPM_PERIOD_TELEMETRY

/** @brief DShot device configuration. */
struct dshot_config {
	/** Bit rate of every channel of the device. */
	enum dshot_speed speed;
};

/**
 * @brief Compute the checksum of a DShot frame.
 *
 * @param data The 12 most significant bits of the frame: the 11-bit value
 *             followed by the telemetry request bit.
 *
 * @return 4-bit checksum.
 */
static inline uint16_t dshot_frame_checksum(uint16_t data)
{
	return (data ^ (data >> 4) ^ (data >> 8)) & 0xFU;
}

/**
 * @brief Build a DShot frame.
 *
 * The value is not range checked against the throttle and command ranges;
 * @ref dshot_set_throttle and @ref dshot_send_command do that.
 *
 * @param value     11-bit payload; bits above bit 10 are ignored.
 * @param telemetry Set the telemetry request bit.
 *
 * @return 16-bit frame, to be transmitted MSB first.
 */
static inline uint16_t dshot_frame_encode(uint16_t value, bool telemetry)
{
	uint16_t data = (uint16_t)(((value & DSHOT_VALUE_MAX) << 1) | (telemetry ? 1U : 0U));

	return (uint16_t)((data << 4) | dshot_frame_checksum(data));
}

/**
 * @brief Get the 11-bit payload of a DShot frame.
 *
 * @param frame 16-bit frame.
 *
 * @return Throttle or command value.
 */
static inline uint16_t dshot_frame_value(uint16_t frame)
{
	return frame >> 5;
}

/**
 * @brief Check whether a value is a valid throttle value.
 *
 * @param throttle Payload value.
 *
 * @retval true for @ref DSHOT_THROTTLE_STOP and values from
 *         @ref DSHOT_THROTTLE_MIN to @ref DSHOT_THROTTLE_MAX.
 * @retval false for command values and out-of-range values.
 */
static inline bool dshot_throttle_is_valid(uint16_t throttle)
{
	return (throttle == DSHOT_THROTTLE_STOP) ||
	       ((throttle >= DSHOT_THROTTLE_MIN) && (throttle <= DSHOT_THROTTLE_MAX));
}

/**
 * @cond INTERNAL_HIDDEN
 *
 * For internal driver use only, skip these in public documentation.
 */

/** @brief Driver API. */
__subsystem struct dshot_driver_api {
	int (*configure)(const struct device *dev, const struct dshot_config *cfg);
	int (*set_frame)(const struct device *dev, uint8_t channel, uint16_t frame);
	int (*send)(const struct device *dev);
	int (*stop)(const struct device *dev);
	int (*send_command)(const struct device *dev, uint32_t channels, enum dshot_command cmd);
	uint8_t (*get_channel_count)(const struct device *dev);
};

/** @endcond */

/**
 * @brief Configure a DShot device.
 *
 * Selects the bit rate of every channel. The driver checks that the
 * underlying hardware can generate the waveform within the protocol
 * tolerances and leaves the previous configuration in place otherwise.
 * The configuration can only change while every channel is stopped.
 *
 * @param dev DShot device.
 * @param cfg Configuration.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p cfg has an invalid speed.
 * @retval -EBUSY if a channel has a nonzero throttle staged.
 * @retval -ENOTSUP if the hardware cannot generate the requested speed.
 * @retval -errno another negative error code on failure.
 */
__syscall int dshot_configure(const struct device *dev, const struct dshot_config *cfg);

static inline int z_impl_dshot_configure(const struct device *dev, const struct dshot_config *cfg)
{
	switch (cfg->speed) {
	case DSHOT_SPEED_150:
	case DSHOT_SPEED_300:
	case DSHOT_SPEED_600:
	case DSHOT_SPEED_1200:
		break;
	default:
		return -EINVAL;
	}

	return DEVICE_API_GET(dshot, dev)->configure(dev, cfg);
}

/**
 * @brief Stage the throttle of a channel.
 *
 * The value is sent by the next @ref dshot_send call and by every call
 * after it until a new value is staged. This call does not block and does
 * not transmit.
 *
 * Values 1 to 47 are special commands and are rejected; use
 * @ref dshot_send_command to send a command.
 *
 * @param dev       DShot device.
 * @param channel   Channel index.
 * @param throttle  @ref DSHOT_THROTTLE_STOP, or a throttle value from
 *                  @ref DSHOT_THROTTLE_MIN to @ref DSHOT_THROTTLE_MAX.
 * @param telemetry Request a telemetry report from the ESC.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p channel or @p throttle is out of range.
 * @retval -EBUSY if @p throttle is nonzero while a command is being sent.
 */
__syscall int dshot_set_throttle(const struct device *dev, uint8_t channel, uint16_t throttle,
				 bool telemetry);

static inline int z_impl_dshot_set_throttle(const struct device *dev, uint8_t channel,
					    uint16_t throttle, bool telemetry)
{
	if (!dshot_throttle_is_valid(throttle)) {
		return -EINVAL;
	}

	return DEVICE_API_GET(dshot, dev)->set_frame(dev, channel,
						      dshot_frame_encode(throttle, telemetry));
}

/**
 * @brief Transmit the staged frame of every channel once.
 *
 * Channels without a staged value send @ref DSHOT_THROTTLE_STOP. Each
 * frame is followed by the inter-frame idle time before the call returns,
 * so back-to-back calls never violate the frame spacing. A channel that
 * fails does not prevent the others from being sent.
 *
 * Whether the channels start together depends on the hardware; see the
 * driver documentation for the skew between channels.
 *
 * @param dev DShot device.
 *
 * @retval 0 on success.
 * @retval -errno the first error reported by a channel.
 */
__syscall int dshot_send(const struct device *dev);

static inline int z_impl_dshot_send(const struct device *dev)
{
	return DEVICE_API_GET(dshot, dev)->send(dev);
}

/**
 * @brief Stop every motor.
 *
 * Stages @ref DSHOT_THROTTLE_STOP on every channel and transmits it once.
 * The application keeps calling @ref dshot_send afterwards to keep the ESCs
 * fed with stop frames.
 *
 * @param dev DShot device.
 *
 * @retval 0 on success.
 * @retval -errno the first error reported by a channel.
 */
__syscall int dshot_stop(const struct device *dev);

static inline int z_impl_dshot_stop(const struct device *dev)
{
	return DEVICE_API_GET(dshot, dev)->stop(dev);
}

/**
 * @brief Send a special command.
 *
 * Sends the command with the repetition count, telemetry bit and trailing
 * delay the protocol defines for it, while the channels not in @p channels
 * receive stop frames. The call blocks until the sequence is complete,
 * which takes up to a few hundred milliseconds for the beacon commands.
 *
 * Commands are only accepted while every channel is stopped.
 *
 * @param dev      DShot device.
 * @param channels Bit mask of the channels that receive the command.
 * @param cmd      Command.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p channels is empty or out of range, or @p cmd is
 *         not a command value.
 * @retval -EBUSY if a channel has a nonzero throttle staged.
 * @retval -errno the first error reported by a channel.
 */
__syscall int dshot_send_command(const struct device *dev, uint32_t channels,
				 enum dshot_command cmd);

static inline int z_impl_dshot_send_command(const struct device *dev, uint32_t channels,
					    enum dshot_command cmd)
{
	if ((uint32_t)cmd > DSHOT_CMD_MAX) {
		return -EINVAL;
	}

	return DEVICE_API_GET(dshot, dev)->send_command(dev, channels, cmd);
}

/**
 * @brief Get the number of channels of a DShot device.
 *
 * @param dev DShot device.
 *
 * @return Number of channels.
 */
__syscall uint8_t dshot_get_channel_count(const struct device *dev);

static inline uint8_t z_impl_dshot_get_channel_count(const struct device *dev)
{
	return DEVICE_API_GET(dshot, dev)->get_channel_count(dev);
}

#ifdef __cplusplus
}
#endif

/** @} */

#include <zephyr/syscalls/dshot.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_DSHOT_H_ */
