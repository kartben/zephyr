/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2018 John Maloney, Bernat Romagosa, and Jens Mönig

// sensorPrims.cpp - Microblocks I2C, SPI, tilt, and temperature primitives
// John Maloney, May 2018

#include <stdio.h>
#include <stdlib.h>

#include "mem.h"
#include "interp.h"

OBJ primAcceleration(int argCount, OBJ *args) {
	int x, y, z;
	x = 3;
	y = 3;
	z = 3;

	int accel = (int) sqrt((x * x) + (y * y) + (z * z));
	return int2obj(accel);
}

static PrimEntry entries[] = {
	{"acceleration", primAcceleration},
	// {"temperature", primMBTemp},
	// {"tiltX", primMBTiltX},
	// {"tiltY", primMBTiltY},
	// {"tiltZ", primMBTiltZ},
	// {"setAccelerometerRange", primSetAccelerometerRange},
	// {"magneticField", primMagneticField},
	// {"touchRead", primTouchRead},
	// {"i2cRead", primI2cRead},
	// {"i2cWrite", primI2cWrite},
	// {"i2cSetClockSpeed", primI2cSetClockSpeed},
	// {"spiExchange", primSPIExchange},
	// {"spiSetup", primSPISetup},
	// {"readDHT", primReadDHT},
	// {"microphone", primMicrophone},
};

void addSensorPrims() {
	addPrimitiveSet("sensors", sizeof(entries) / sizeof(PrimEntry), entries);
}
