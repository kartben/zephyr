/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_IDS_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_IDS_H_

#include <stdint.h>

/*
 * Wire constants of the Android Auto projection protocol as used between a
 * phone and a head unit. Written from the observed wire format; values marked
 * "verify" are confirmed against the virtual head unit in scripts/.
 */

/* Frame header: channel, flags, u16 length, [u32 total length], payload */
#define AA_FRAME_FIRST     0x01U
#define AA_FRAME_LAST      0x02U
#define AA_FRAME_BULK      (AA_FRAME_FIRST | AA_FRAME_LAST)
#define AA_FRAME_CONTROL   0x04U
#define AA_FRAME_ENCRYPTED 0x08U
#define AA_FRAME_HDR_LEN   4U
#define AA_FRAME_TOTAL_LEN 4U
#define AA_FRAME_MAX_HDR   (AA_FRAME_HDR_LEN + AA_FRAME_TOTAL_LEN)

#define AA_CHANNEL_CONTROL 0U

#define AA_PROTO_MAJOR 1U

/* Control channel messages */
enum aa_ctrl_msg {
	AA_CTRL_VERSION_REQUEST = 0x0001,
	AA_CTRL_VERSION_RESPONSE = 0x0002,
	AA_CTRL_SSL_HANDSHAKE = 0x0003,
	AA_CTRL_AUTH_COMPLETE = 0x0004,
	AA_CTRL_SERVICE_DISCOVERY_REQUEST = 0x0005,
	AA_CTRL_SERVICE_DISCOVERY_RESPONSE = 0x0006,
	AA_CTRL_CHANNEL_OPEN_REQUEST = 0x0007,
	AA_CTRL_CHANNEL_OPEN_RESPONSE = 0x0008,
	AA_CTRL_PING_REQUEST = 0x000b,
	AA_CTRL_PING_RESPONSE = 0x000c,
	AA_CTRL_NAVIGATION_FOCUS_REQUEST = 0x000d,
	AA_CTRL_NAVIGATION_FOCUS_RESPONSE = 0x000e,
	AA_CTRL_SHUTDOWN_REQUEST = 0x000f,
	AA_CTRL_SHUTDOWN_RESPONSE = 0x0010,
	AA_CTRL_VOICE_SESSION_REQUEST = 0x0011,
	AA_CTRL_AUDIO_FOCUS_REQUEST = 0x0012,
	AA_CTRL_AUDIO_FOCUS_RESPONSE = 0x0013,
};

/* Audio/video channel messages */
enum aa_av_msg {
	AA_AV_MEDIA_WITH_TIMESTAMP_INDICATION = 0x0000,
	AA_AV_MEDIA_INDICATION = 0x0001,
	AA_AV_SETUP_REQUEST = 0x8000,
	AA_AV_START_INDICATION = 0x8001,
	AA_AV_STOP_INDICATION = 0x8002,
	AA_AV_SETUP_RESPONSE = 0x8003,
	AA_AV_MEDIA_ACK_INDICATION = 0x8004,
	AA_AV_MICROPHONE_REQUEST = 0x8005,
	AA_AV_MICROPHONE_RESPONSE = 0x8006,
	AA_AV_VIDEO_FOCUS_REQUEST = 0x8007,
	AA_AV_VIDEO_FOCUS_INDICATION = 0x8008,
};

/* Input channel messages */
enum aa_input_msg {
	AA_INPUT_EVENT_INDICATION = 0x8001,
	AA_INPUT_BINDING_REQUEST = 0x8002,
	AA_INPUT_BINDING_RESPONSE = 0x8003,
};

/* Sensor channel messages */
enum aa_sensor_msg {
	AA_NAV_STATUS_START = 0x8001,
	AA_NAV_STATUS_STOP = 0x8002,
	AA_NAV_STATUS = 0x8003,
	AA_NAV_TURN_EVENT = 0x8004,
	AA_NAV_DISTANCE_EVENT = 0x8005,
	AA_NAV_STATE = 0x8006,
};

/* Cluster form the head unit asks the phone for */
#define AA_NAV_CLUSTER_IMAGE 1
#define AA_NAV_CLUSTER_ENUM  2

/* NavigationNextTurnEvent.turn_side */
#define AA_NAV_TURN_LEFT  1
#define AA_NAV_TURN_RIGHT 2

enum {
	AA_SENSOR_START_REQUEST = 0x8001,
	AA_SENSOR_START_RESPONSE = 0x8002,
	AA_SENSOR_EVENT_INDICATION = 0x8003,
};

/* Enumerations carried in protobuf fields */
#define AA_STATUS_OK 0

#define AA_STREAM_TYPE_AUDIO 1
#define AA_STREAM_TYPE_VIDEO 3

/* Where the driver sits, as the service discovery response reports it */
#define AA_DRIVER_POSITION_LEFT   0
#define AA_DRIVER_POSITION_RIGHT  1
#define AA_DRIVER_POSITION_CENTER 2

/* Audio stream kinds a head unit provides */
#define AA_AUDIO_TYPE_GUIDANCE 1
#define AA_AUDIO_TYPE_SYSTEM   2
#define AA_AUDIO_TYPE_MEDIA    3

/* Audio focus the phone asks for, and the state the head unit reports back */
#define AA_AUDIO_FOCUS_RELEASE     4
#define AA_AUDIO_FOCUS_STATE_GAIN  1
#define AA_AUDIO_FOCUS_STATE_LOSS  3

#define AA_VIDEO_RESOLUTION_800x480   1
#define AA_VIDEO_RESOLUTION_1280x720  2
#define AA_VIDEO_RESOLUTION_1920x1080 3

/* The faster rate is the lower value; asking for each in turn and
 * counting the pictures that came back is what settled the order.
 */
#define AA_VIDEO_FPS_60 1
#define AA_VIDEO_FPS_30 2

#define AA_MEDIA_STATUS_FAIL 1
#define AA_MEDIA_STATUS_OK   2

#define AA_VIDEO_FOCUS_FOCUSED          1
#define AA_VIDEO_FOCUS_UNFOCUSED        2
#define AA_VIDEO_FOCUS_NATIVE_TRANSIENT 3

#define AA_TOUCH_ACTION_PRESS        0
#define AA_TOUCH_ACTION_RELEASE      1
#define AA_TOUCH_ACTION_DRAG         2
#define AA_TOUCH_ACTION_POINTER_DOWN 5
#define AA_TOUCH_ACTION_POINTER_UP   6

#define AA_SENSOR_TYPE_PARKING_BRAKE  7
#define AA_SENSOR_TYPE_GEAR           8
#define AA_SENSOR_TYPE_NIGHT_DATA     10
#define AA_SENSOR_TYPE_DRIVING_STATUS 13

/* Driving status values */
#define AA_DRIVING_STATUS_UNRESTRICTED 0

/* Gear selection, as the sensor channel reports it */
#define AA_GEAR_NEUTRAL 0
#define AA_GEAR_DRIVE   100
#define AA_GEAR_PARK    101

/* Android key codes the sample binds to */
#define AA_KEYCODE_HOME           3
#define AA_KEYCODE_BACK           4
#define AA_KEYCODE_DPAD_UP        19
#define AA_KEYCODE_DPAD_DOWN      20
#define AA_KEYCODE_DPAD_LEFT      21
#define AA_KEYCODE_DPAD_RIGHT     22
#define AA_KEYCODE_DPAD_CENTER    23
#define AA_KEYCODE_VOLUME_UP      24
#define AA_KEYCODE_VOLUME_DOWN    25
#define AA_KEYCODE_MEDIA_PLAY_PAUSE 85
#define AA_KEYCODE_MEDIA_NEXT     87
#define AA_KEYCODE_MEDIA_PREVIOUS 88

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_IDS_H_ */
