/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 PIE version of ppa_pack_line() in aa_scale.c:
 *
 * void aa_pie_pack_line(uint32_t *out, const uint8_t *luma, const uint8_t *chroma,
 *                       uint16_t w)
 *
 * Thirty-two pixels at a time: the luma samples are split into even and odd
 * ones, and those and the sixteen chroma samples of the pixels are zipped three
 * ways into the chroma, luma, luma order of Espressif's packed 4:2:0. All three
 * pointers must be on 16 bytes and w a multiple of 32. The stores do not
 * allocate cache lines, so nothing is read from memory for the output.
 *
 * The vector registers are not saved when a thread is switched out, so only
 * a thread that no other thread using them can preempt may call this.
 */
	.option norvc
	.text

	.balign 4
	.global aa_pie_pack_line
	.type aa_pie_pack_line, @function
aa_pie_pack_line:
	li	t3, 1
	csrw	0x7f2, t3
	li	t3, 0x03
	esp.movx.w.cfg	t3
	srli	a3, a3, 5
	beqz	a3, 2f
1:	esp.vld.128.ip	q0, a1, 16
	esp.vld.128.ip	q1, a1, 16
	esp.vunzip.8	q0, q1
	esp.vld.128.ip	q2, a2, 16
	esp.vzipt.8	q2, q0, q1
	esp.vst.128.ip	q2, a0, 16
	esp.vst.128.ip	q0, a0, 16
	esp.vst.128.ip	q1, a0, 16
	addi	a3, a3, -1
	bnez	a3, 1b
2:	ret
	.size aa_pie_pack_line, . - aa_pie_pack_line
