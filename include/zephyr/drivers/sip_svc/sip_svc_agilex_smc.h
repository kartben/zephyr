/*
 * Copyright (c) 2022-2023, Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SIP_SVC_AGILEX_SMC_H_
#define ZEPHYR_INCLUDE_SIP_SVC_AGILEX_SMC_H_

/**
 * @file
 * @brief Intel SoC FPGA Agilex customized Arm SiP Services
 *        SMC protocol.
 * @ingroup sip_svc
 */

/* @brief SMC return status
 */

/** Invalid status */
#define SMC_STATUS_INVALID     0xFFFFFFFF
/** Request executed successfully */
#define SMC_STATUS_OKAY	       0
/** Request is still in progress */
#define SMC_STATUS_BUSY	       1
/** Request was rejected */
#define SMC_STATUS_REJECT      2
/** No response from target hardware yet */
#define SMC_STATUS_NO_RESPONSE 3
/** Error occurred while executing the request */
#define SMC_STATUS_ERROR       4

/* @brief SMC Intel Header at a1
 *
 * bit
 *  7: 0   Transaction ID
 * 59: 8   Reserved
 * 63:60   Version
 */
/** Current SMC platform protocol version */
#define SMC_PLAT_PROTO_VER 0x0

/** Bit offset of the transaction ID field in the header */
#define SMC_PLAT_PROTO_HEADER_TRANS_ID_OFFSET 0
/** Mask of the transaction ID field in the header */
#define SMC_PLAT_PROTO_HEADER_TRANS_ID_MASK   0xFF

/** Bit offset of the version field in the header */
#define SMC_PLAT_PROTO_HEADER_VER_OFFSET 60
/** Mask of the version field in the header */
#define SMC_PLAT_PROTO_HEADER_VER_MASK	 0xF

/** SMC platform protocol header with the current protocol version set */
#define SMC_PLAT_PROTO_HEADER                                                                      \
	((SMC_PLAT_PROTO_VER & SMC_PLAT_PROTO_HEADER_VER_MASK) << SMC_PLAT_PROTO_HEADER_VER_OFFSET)

/**
 * @brief Set the transaction ID field in an SMC platform protocol header
 *
 * @param header Header value to update
 * @param trans_id Transaction ID to set
 */
#define SMC_PLAT_PROTO_HEADER_SET_TRANS_ID(header, trans_id)                                       \
	(header) &=                                                                                \
		~(SMC_PLAT_PROTO_HEADER_TRANS_ID_MASK << SMC_PLAT_PROTO_HEADER_TRANS_ID_OFFSET);   \
	(header) |= (((trans_id)&SMC_PLAT_PROTO_HEADER_TRANS_ID_MASK)                              \
		     << SMC_PLAT_PROTO_HEADER_TRANS_ID_OFFSET);

/* @brief SYNC SMC Function IDs
 */

/** Get the SiP services version */
#define SMC_FUNC_ID_GET_SVC_VERSION 0xC2000400
/** Read a register */
#define SMC_FUNC_ID_REG_READ	    0xC2000401
/** Write a register */
#define SMC_FUNC_ID_REG_WRITE	    0xC2000402
/** Update (read-modify-write) a register */
#define SMC_FUNC_ID_REG_UPDATE	    0xC2000403
/** Enable or disable the HPS bridges */
#define SMC_FUNC_ID_SET_HPS_BRIDGES 0xC2000404
/** Set the Remote System Update (RSU) address */
#define SMC_FUNC_ID_RSU_UPDATE_ADDR 0xC2000405

/* @brief ASYNC SMC Function IDs
 */

/** Send a command to the SDM mailbox */
#define SMC_FUNC_ID_MAILBOX_SEND_COMMAND  0xC2000420
/** Poll a response from the SDM mailbox */
#define SMC_FUNC_ID_MAILBOX_POLL_RESPONSE 0xC2000421

/** SDM mailbox CANCEL command */
#define MAILBOX_CANCEL_COMMAND 0x03

#endif /* ZEPHYR_INCLUDE_SIP_SVC_AGILEX_SMC_H_ */
