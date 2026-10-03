/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KITE_RUSH_BEACON_H_
#define KITE_RUSH_BEACON_H_

#include <stdint.h>

#include "game.h"

/* Starts advertising the game over Bluetooth LE */
int beacon_init(const struct game *g);
/* Puts the score in the advertising data, at most once a second */
void beacon_update(const struct game *g, int64_t now_ms);

#endif /* KITE_RUSH_BEACON_H_ */
