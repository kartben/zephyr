/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "dshot_command.h"

/* ESCs act on a command once they have seen it in 6 consecutive frames */
#define DSHOT_COMMAND_REPEAT 6U

/* Settings commands get the margin Betaflight and PX4 use */
#define DSHOT_COMMAND_REPEAT_SETTINGS 10U

#define DSHOT_BEACON_DELAY_US        260000U
#define DSHOT_ESC_INFO_DELAY_US      12000U
#define DSHOT_SAVE_SETTINGS_DELAY_US 35000U

void dshot_command_get_info(enum dshot_command cmd, struct dshot_command_info *info)
{
	*info = (struct dshot_command_info){
		.repeat = DSHOT_COMMAND_REPEAT,
		/* ESCs drop command frames without the telemetry bit */
		.telemetry = (cmd != DSHOT_CMD_MOTOR_STOP),
		.delay_us = DSHOT_COMMAND_INTERVAL_US,
	};

	switch (cmd) {
	case DSHOT_CMD_MOTOR_STOP:
		info->repeat = 1U;
		break;
	case DSHOT_CMD_BEACON1:
	case DSHOT_CMD_BEACON2:
	case DSHOT_CMD_BEACON3:
	case DSHOT_CMD_BEACON4:
	case DSHOT_CMD_BEACON5:
		/* A beacon plays on the first frame and lasts up to 260 ms */
		info->repeat = 1U;
		info->delay_us = DSHOT_BEACON_DELAY_US;
		break;
	case DSHOT_CMD_ESC_INFO:
		info->delay_us = DSHOT_ESC_INFO_DELAY_US;
		break;
	case DSHOT_CMD_SAVE_SETTINGS:
		info->repeat = DSHOT_COMMAND_REPEAT_SETTINGS;
		info->delay_us = DSHOT_SAVE_SETTINGS_DELAY_US;
		break;
	case DSHOT_CMD_SPIN_DIRECTION_1:
	case DSHOT_CMD_SPIN_DIRECTION_2:
	case DSHOT_CMD_3D_MODE_OFF:
	case DSHOT_CMD_3D_MODE_ON:
	case DSHOT_CMD_EXTENDED_TELEMETRY_ENABLE:
	case DSHOT_CMD_EXTENDED_TELEMETRY_DISABLE:
	case DSHOT_CMD_SPIN_DIRECTION_NORMAL:
	case DSHOT_CMD_SPIN_DIRECTION_REVERSED:
		info->repeat = DSHOT_COMMAND_REPEAT_SETTINGS;
		break;
	case DSHOT_CMD_AUDIO_STREAM_MODE_ON_OFF:
	case DSHOT_CMD_SILENT_MODE_ON_OFF:
		/* Toggles: a second frame would undo the first one */
		info->repeat = 1U;
		break;
	default:
		break;
	}
}
