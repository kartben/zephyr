/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * The parts of h264bsd that run for every macroblock. Those that run once per
 * slice, picture or stream stay in flash.
 */
*libh264bsd.a:h264bsd_cavlc.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_deblocking.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_image.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_inter_prediction.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_intra_prediction.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_macroblock_layer.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_neighbour.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_reconstruct.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_slice_data.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_stream.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_transform.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_util.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:h264bsd_vlc.c.obj(.literal .text .literal.* .text.*)
*libh264bsd.a:*.pie.obj(.literal .text .literal.* .text.*)
