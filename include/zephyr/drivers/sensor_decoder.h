/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup sensor_decoder_helpers
 * @brief Helpers for implementing sensor decoders.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_DECODER_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_DECODER_H_

/**
 * @brief Helpers for implementing sensor decoders
 * @defgroup sensor_decoder_helpers Sensor decoder helpers
 * @ingroup sensor_interface
 * @experimental
 * @{
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/dsp/types.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert an output data rate in millihertz to a sample period in nanoseconds
 *
 * The computation is done in 64 bits, so fractional rates such as 3.906 Hz can be written
 * as @c SENSOR_ODR_MHZ_TO_PERIOD_NS(3906) without overflowing.
 *
 * @param odr_mhz Output data rate in millihertz
 * @return Sample period in nanoseconds, as a @c uint64_t
 */
#define SENSOR_ODR_MHZ_TO_PERIOD_NS(odr_mhz) (UINT64_C(1000000000000) / (uint64_t)(odr_mhz))

/**
 * @brief Compute the factor that scales a raw sample to a q31 value
 *
 * One LSB of the raw sample equals @p num / @p den of the channel's base unit (for example
 * micro-m/s^2 per LSB for an accelerometer). The result is rounded to the nearest integer and
 * can be used in static initializers. Multiplying a raw sample by the factor yields a q31
 * value to be interpreted with @p shift.
 *
 * Example for an accelerometer with 256 LSB/g read with a shift of 5:
 * @code{.c}
 * SENSOR_Q31_SCALE(SENSOR_G, 256 * 1000000LL, 5)
 * @endcode
 *
 * When one LSB is small compared to 2^shift, rounding the factor loses precision; use
 * sensor_raw_to_q31_ratio() instead.
 *
 * @param num Numerator of the value of one LSB, positive
 * @param den Denominator of the value of one LSB, positive
 * @param shift Shift of the q31 output
 * @return Scale factor as an @c int32_t
 */
#define SENSOR_Q31_SCALE(num, den, shift)                                                          \
	((int32_t)((((int64_t)(num) * ((int64_t)1 << (31 - (shift)))) + ((int64_t)(den) / 2)) /    \
		   (int64_t)(den)))

/** @cond INTERNAL_HIDDEN */
static inline q31_t z_sensor_q31_sat(int64_t value)
{
	return (q31_t)CLAMP(value, INT32_MIN, INT32_MAX);
}
/** @endcond */

/**
 * @brief Convert a two's complement raw sample to q31
 *
 * The result saturates when it does not fit, which happens when the shift used for @p scale
 * does not cover the full range of the sample.
 *
 * @param raw Raw sample, right-justified; bits above @p bits are ignored
 * @param bits Number of significant bits in @p raw, sign bit included (1 to 32)
 * @param scale Scale factor computed with @ref SENSOR_Q31_SCALE
 * @return The sample in q31 format
 */
static inline q31_t sensor_raw_to_q31(uint32_t raw, uint8_t bits, int32_t scale)
{
	return z_sensor_q31_sat((int64_t)sign_extend(raw, (uint8_t)(bits - 1U)) * scale);
}

/**
 * @brief Convert a two's complement raw sample to q31 with an exact ratio
 *
 * Computes raw * @p num / @p den * 2^(31 - @p shift) without rounding the ratio first, for
 * high-resolution samples or scales that depend on runtime data. The result is truncated
 * toward zero and saturates when it does not fit.
 *
 * @param raw Raw sample, right-justified; bits above @p bits are ignored
 * @param bits Number of significant bits in @p raw, sign bit included (1 to 32)
 * @param num Numerator of the value of one LSB
 * @param den Denominator of the value of one LSB, positive
 * @param shift Shift of the q31 output, 0 to 31
 * @return The sample in q31 format
 */
static inline q31_t sensor_raw_to_q31_ratio(uint32_t raw, uint8_t bits, int32_t num, int32_t den,
					    int8_t shift)
{
	const int64_t value = (int64_t)sign_extend(raw, (uint8_t)(bits - 1U)) * num;
	const int64_t whole = value / den;
	const int64_t rem = value % den;
	const uint8_t lshift = (uint8_t)(31 - shift);
	const int64_t limit = INT64_C(1) << (62U - lshift);

	if (whole >= limit || whole <= -limit) {
		return (whole > 0) ? INT32_MAX : INT32_MIN;
	}

	return z_sensor_q31_sat(whole * (INT64_C(1) << lshift) +
				(rem * (INT64_C(1) << lshift)) / den);
}

/**
 * @brief One reading produced by a @ref sensor_decode_frame_t callback
 */
struct sensor_frame_reading {
	/** Index of the reading within the frame */
	uint8_t index;
	/**
	 * Timestamp of the reading in nanoseconds. It is set from the sample period before the
	 * call; the callback may overwrite it, for example with a timestamp carried by the frame.
	 */
	uint64_t timestamp_ns;
	/**
	 * Output values, zeroed before the call: @ref sensor_raw_frames.num_values of them.
	 */
	q31_t *values;
};

/**
 * @brief Decode the readings of one raw frame
 *
 * Called by sensor_decode_frames() and sensor_raw_frames_count(). With @p reading set to NULL
 * the callback only reports how many readings the frame holds for @p chan_spec: 0 when the
 * frame does not carry that channel, for example a frame of another sensor in a tagged FIFO.
 * Otherwise it also writes reading number @p reading->index of the frame.
 *
 * @param[in]     frame     Start of the raw frame
 * @param[in]     chan_spec Channel to decode
 * @param[in]     user_data Pointer given in @ref sensor_raw_frames.user_data
 * @param[in,out] reading   Reading to write, or NULL to count
 *
 * @return Number of readings the frame holds for @p chan_spec, up to 255
 * @retval -ENODATA The reading is invalid: it is dropped but keeps its timestamp slot
 * @retval -ENOTSUP Channel not supported
 * @retval <0 Other negative errno code on failure
 */
typedef int (*sensor_decode_frame_t)(const uint8_t *frame, struct sensor_chan_spec chan_spec,
				     const void *user_data, struct sensor_frame_reading *reading);

/**
 * @brief Get the length of one frame of a buffer of variable-size frames
 *
 * @param frame     Start of the frame
 * @param remaining Number of bytes from @p frame to the end of the buffer
 * @param user_data Pointer given in @ref sensor_raw_frames.user_data
 *
 * @return Length of the frame in bytes; a length above @p remaining ends the buffer
 * @retval 0 End of the data, for example padding or a terminator
 * @retval <0 Negative errno code on failure
 */
typedef int (*sensor_frame_len_t)(const uint8_t *frame, size_t remaining, const void *user_data);

/**
 * @brief Description of a buffer of raw frames
 *
 * A decoder fills this structure from its encoded buffer for the requested channel and passes
 * it to sensor_decode_frames() and sensor_raw_frames_count(). Fields that depend on the
 * channel, such as @ref shift or @ref period_ns, are set for that channel. A single sample
 * read is described as one frame with a period of 0.
 *
 * Readings are timestamped from @ref timestamp_ns, the timestamp of the newest reading of the
 * channel, going back one period per reading. Frames are at most 16 MiB from the start of
 * the buffer.
 */
struct sensor_raw_frames {
	/** Start of the first (oldest) frame */
	const uint8_t *frames;
	/** Number of bytes of frames; a trailing partial frame is ignored */
	size_t size;
	/** Size of every frame in bytes, used when @ref frame_len is NULL */
	size_t frame_size;
	/** Function giving the size of each frame, for buffers of variable-size frames */
	sensor_frame_len_t frame_len;
	/** Function decoding one frame */
	sensor_decode_frame_t decode_frame;
	/** Opaque pointer passed to @ref decode_frame and @ref frame_len */
	const void *user_data;
	/** Timestamp of the newest reading in nanoseconds */
	uint64_t timestamp_ns;
	/** Sample period in nanoseconds, divided by @ref period_den when that is not 0 */
	uint64_t period_ns;
	/** Divisor of @ref period_ns, for periods that are not a whole number of nanoseconds */
	uint32_t period_den;
	/** Shift of the q31 output values */
	int8_t shift;
	/**
	 * Number of q31 values per reading: 1, 3 or 4. 0 selects it from the channel, as
	 * sensor_decode_frames_size_info() does.
	 */
	uint8_t num_values;
	/** Highest channel index accepted */
	uint8_t max_chan_idx;
};

/**
 * @brief Decode readings from a buffer of raw frames
 *
 * Implements @ref sensor_decoder_api.decode. The output is a @ref sensor_q31_data,
 * @ref sensor_three_axis_data or @ref sensor_game_rotation_vector_data depending on the
 * number of values per reading. The header timestamp is the one of the first reading written.
 * A batch ends early at a reading older than that or more than UINT32_MAX ns newer, and the
 * next call continues from it. @p fit is opaque, 0 to start from the first reading.
 *
 * @param[in]     frames    Description of the frames
 * @param[in]     chan_spec Channel to decode
 * @param[in,out] fit       Iterator
 * @param[in]     max_count Maximum number of readings to decode
 * @param[out]    data_out  Output buffer
 *
 * @return Number of readings decoded, 0 once all readings have been decoded or when the
 *         buffer holds no frame
 * @retval -ENODATA The buffer holds frames but none carries the channel
 * @retval -ENOTSUP Channel not supported
 * @retval -EINVAL Invalid description of the frames
 * @retval <0 Error returned by @ref sensor_raw_frames.decode_frame or
 *         @ref sensor_raw_frames.frame_len
 */
int sensor_decode_frames(const struct sensor_raw_frames *frames, struct sensor_chan_spec chan_spec,
			 uint32_t *fit, uint16_t max_count, void *data_out);

/**
 * @brief Count the readings of a channel in a buffer of raw frames
 *
 * Implements @ref sensor_decoder_api.get_frame_count with the description of the frames used
 * for sensor_decode_frames(), so both agree.
 *
 * @param[in]  frames      Description of the frames
 * @param[in]  chan_spec   Channel to count
 * @param[out] frame_count Number of readings, saturated to UINT16_MAX
 *
 * @retval 0 Success
 * @retval -ENOTSUP Channel not supported
 * @retval -EINVAL Invalid description of the frames
 * @retval <0 Error returned by @ref sensor_raw_frames.decode_frame or
 *         @ref sensor_raw_frames.frame_len
 */
int sensor_raw_frames_count(const struct sensor_raw_frames *frames,
			    struct sensor_chan_spec chan_spec, uint16_t *frame_count);

/**
 * @brief Get the output sizes of sensor_decode_frames()
 *
 * Implements @ref sensor_decoder_api.get_size_info for channels decoded with
 * sensor_decode_frames(). With @p num_values set to 0, @ref SENSOR_CHAN_GAME_ROTATION_VECTOR
 * has four values; the channels matched by @ref SENSOR_CHANNEL_3_AXIS,
 * @ref SENSOR_CHAN_GRAVITY_VECTOR and @ref SENSOR_CHAN_GBIAS_XYZ have three; other channels
 * have one, except @ref SENSOR_CHAN_PROX and @ref SENSOR_CHAN_GAUGE_CYCLE_COUNT which are not
 * q31 channels.
 *
 * @param[in]  chan_spec  Channel
 * @param[in]  num_values Number of values per reading, as @ref sensor_raw_frames.num_values
 * @param[out] base_size  Size of the output for one reading
 * @param[out] frame_size Size of every additional reading
 *
 * @retval 0 Success
 * @retval -ENOTSUP Channel not supported
 */
int sensor_decode_frames_size_info(struct sensor_chan_spec chan_spec, uint8_t num_values,
				   size_t *base_size, size_t *frame_size);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_DECODER_H_ */
