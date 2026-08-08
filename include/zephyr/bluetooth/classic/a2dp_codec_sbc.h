/** @file
 * @brief Advance Audio Distribution Profile - SBC Codec header.
 * @ingroup bt_a2dp
 */
/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2021 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef ZEPHYR_INCLUDE_BLUETOOTH_CLASSIC_A2DP_CODEC_SBC_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_CLASSIC_A2DP_CODEC_SBC_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Sampling Frequency */
/** SBC sampling frequency 16000 Hz */
#define A2DP_SBC_SAMP_FREQ_16000 BIT(7)
/** SBC sampling frequency 32000 Hz */
#define A2DP_SBC_SAMP_FREQ_32000 BIT(6)
/** SBC sampling frequency 44100 Hz */
#define A2DP_SBC_SAMP_FREQ_44100 BIT(5)
/** SBC sampling frequency 48000 Hz */
#define A2DP_SBC_SAMP_FREQ_48000 BIT(4)

/* Channel Mode */
/** SBC channel mode Mono */
#define A2DP_SBC_CH_MODE_MONO   BIT(3)
/** SBC channel mode Dual Channel */
#define A2DP_SBC_CH_MODE_DUAL   BIT(2)
/** SBC channel mode Stereo */
#define A2DP_SBC_CH_MODE_STEREO BIT(1)
/** SBC channel mode Joint Stereo */
#define A2DP_SBC_CH_MODE_JOINT  BIT(0)

/* Block Length */
/** SBC block length 4 */
#define A2DP_SBC_BLK_LEN_4  BIT(7)
/** SBC block length 8 */
#define A2DP_SBC_BLK_LEN_8  BIT(6)
/** SBC block length 12 */
#define A2DP_SBC_BLK_LEN_12 BIT(5)
/** SBC block length 16 */
#define A2DP_SBC_BLK_LEN_16 BIT(4)

/* Subbands */
/** SBC 4 subbands */
#define A2DP_SBC_SUBBAND_4 BIT(3)
/** SBC 8 subbands */
#define A2DP_SBC_SUBBAND_8 BIT(2)

/* Allocation Method */
/** SBC bit allocation method SNR */
#define A2DP_SBC_ALLOC_MTHD_SNR      BIT(1)
/** SBC bit allocation method Loudness */
#define A2DP_SBC_ALLOC_MTHD_LOUDNESS BIT(0)

/**
 * @brief Get the Sampling Frequency field of SBC codec parameters.
 *
 * @param cap Pointer to SBC codec parameters, see @ref bt_a2dp_codec_sbc_params.
 *
 * @return Sampling Frequency field value.
 */
#define BT_A2DP_SBC_SAMP_FREQ(cap)    ((cap->config[0] >> 4) & 0x0f)
/**
 * @brief Get the Channel Mode field of SBC codec parameters.
 *
 * @param cap Pointer to SBC codec parameters, see @ref bt_a2dp_codec_sbc_params.
 *
 * @return Channel Mode field value.
 */
#define BT_A2DP_SBC_CHAN_MODE(cap)    ((cap->config[0]) & 0x0f)
/**
 * @brief Get the Block Length field of SBC codec parameters.
 *
 * @param cap Pointer to SBC codec parameters, see @ref bt_a2dp_codec_sbc_params.
 *
 * @return Block Length field value.
 */
#define BT_A2DP_SBC_BLK_LEN(cap)      ((cap->config[1] >> 4) & 0x0f)
/**
 * @brief Get the Subbands field of SBC codec parameters.
 *
 * @param cap Pointer to SBC codec parameters, see @ref bt_a2dp_codec_sbc_params.
 *
 * @return Subbands field value.
 */
#define BT_A2DP_SBC_SUB_BAND(cap)     ((cap->config[1] >> 2) & 0x03)
/**
 * @brief Get the Allocation Method field of SBC codec parameters.
 *
 * @param cap Pointer to SBC codec parameters, see @ref bt_a2dp_codec_sbc_params.
 *
 * @return Allocation Method field value.
 */
#define BT_A2DP_SBC_ALLOC_MTHD(cap)   ((cap->config[1]) & 0x03)
/** Minimum allowed SBC bitpool value */
#define BT_A2DP_SBC_MIN_BITPOOL_VALUE  2
/** Maximum allowed SBC bitpool value */
#define BT_A2DP_SBC_MAX_BITPOOL_VALUE  250

/** @brief SBC Codec */
struct bt_a2dp_codec_sbc_params {
	/** First two octets of configuration */
	uint8_t config[2];
	/** Minimum Bitpool Value */
	uint8_t min_bitpool;
	/** Maximum Bitpool Value */
	uint8_t max_bitpool;
} __packed;

/** If the F bit is set to 0, this field indicates the number of frames contained
 *  in this packet. If the F bit is set to 1, this field indicates the number
 *  of remaining fragments, including the current fragment.
 *  Therefore, the last counter value shall be one.
 */
#define BT_A2DP_SBC_MEDIA_HDR_NUM_FRAMES_GET(hdr) FIELD_GET(GENMASK(3, 0), (hdr))
/** Set to 1 for the last packet of a fragmented SBC frame, otherwise set to 0. */
#define BT_A2DP_SBC_MEDIA_HDR_L_GET(hdr)          FIELD_GET(BIT(5), (hdr))
/** Set to 1 for the starting packet of a fragmented SBC frame, otherwise set to 0. */
#define BT_A2DP_SBC_MEDIA_HDR_S_GET(hdr)          FIELD_GET(BIT(6), (hdr))
/** Set to 1 if the SBC frame is fragmented, otherwise set to 0. */
#define BT_A2DP_SBC_MEDIA_HDR_F_GET(hdr)          FIELD_GET(BIT(7), (hdr))

/** If the F bit is set to 0, this field indicates the number of frames contained
 *  in this packet. If the F bit is set to 1, this field indicates the number
 *  of remaining fragments, including the current fragment.
 *  Therefore, the last counter value shall be one.
 */
#define BT_A2DP_SBC_MEDIA_HDR_NUM_FRAMES_SET(hdr, val)\
	hdr = ((hdr) & ~GENMASK(3, 0)) | FIELD_PREP(GENMASK(3, 0), (val))
/** Set to 1 for the last packet of a fragmented SBC frame, otherwise set to 0. */
#define BT_A2DP_SBC_MEDIA_HDR_L_SET(hdr, val)\
	hdr = ((hdr) & ~BIT(5)) | FIELD_PREP(BIT(5), (val))
/** Set to 1 for the starting packet of a fragmented SBC frame, otherwise set to 0. */
#define BT_A2DP_SBC_MEDIA_HDR_S_SET(hdr, val)\
	hdr = ((hdr) & ~BIT(6)) | FIELD_PREP(BIT(6), (val))
/** Set to 1 if the SBC frame is fragmented, otherwise set to 0. */
#define BT_A2DP_SBC_MEDIA_HDR_F_SET(hdr, val)\
	hdr = ((hdr) & ~BIT(7)) | FIELD_PREP(BIT(7), (val))

/**
 * @brief Encode an SBC media payload header.
 *
 * @param num_frames Number of frames contained in the packet if @p f is 0,
 *                   otherwise number of remaining fragments, including the
 *                   current fragment.
 * @param l Set to 1 for the last packet of a fragmented SBC frame, otherwise 0.
 * @param s Set to 1 for the starting packet of a fragmented SBC frame, otherwise 0.
 * @param f Set to 1 if the SBC frame is fragmented, otherwise 0.
 *
 * @return Encoded SBC media payload header value.
 */
#define BT_A2DP_SBC_MEDIA_HDR_ENCODE(num_frames, l, s, f)\
	FIELD_PREP(GENMASK(3, 0), num_frames) | FIELD_PREP(BIT(5), l) |\
	FIELD_PREP(BIT(6), s) | FIELD_PREP(BIT(7), f)

/** @brief get channel num of a2dp sbc config.
 *
 *  @param sbc_codec The a2dp sbc parameter.
 *
 *  @return the channel num.
 */
uint8_t bt_a2dp_sbc_get_channel_num(struct bt_a2dp_codec_sbc_params *sbc_codec);

/** @brief get channel mode of a2dp sbc config.
 *
 *  @param sbc_codec The a2dp sbc parameter.
 *
 *  @return the channel mode.
 */
enum sbc_ch_mode bt_a2dp_sbc_get_channel_mode(struct bt_a2dp_codec_sbc_params *sbc_codec);

/** @brief get sample rate of a2dp sbc config.
 *
 *  @param sbc_codec The a2dp sbc parameter.
 *
 *  @return the sample rate.
 */
uint32_t bt_a2dp_sbc_get_sampling_frequency(struct bt_a2dp_codec_sbc_params *sbc_codec);

/** @brief get subband num of a2dp sbc config.
 *
 *  @param sbc_codec The a2dp sbc parameter.
 *
 *  @return the subband num.
 */
uint8_t bt_a2dp_sbc_get_subband_num(struct bt_a2dp_codec_sbc_params *sbc_codec);

/** @brief get block length of a2dp sbc config.
 *
 *  @param sbc_codec The a2dp sbc parameter.
 *
 *  @return the block length.
 */
uint8_t bt_a2dp_sbc_get_block_length(struct bt_a2dp_codec_sbc_params *sbc_codec);

/** @brief get allocation method of a2dp sbc config.
 *
 *  @param sbc_codec The a2dp sbc parameter.
 *
 *  @return the allocation method.
 */
enum sbc_alloc_mthd bt_a2dp_sbc_get_allocation_method(struct bt_a2dp_codec_sbc_params *sbc_codec);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_CLASSIC_A2DP_CODEC_SBC_H_ */
