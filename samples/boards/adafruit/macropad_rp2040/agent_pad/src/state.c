/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "agent_pad.h"

static struct agent agents[AGENT_SLOTS];
static int layer;
static char note[NOTE_LEN];
static bool armed;
static atomic_t seq;

static K_MUTEX_DEFINE(lock);

static void bump(void)
{
	atomic_inc(&seq);
}

enum agent_status state_agent_set(int slot, enum agent_status status, const char *label)
{
	enum agent_status old;

	if (slot < 0 || slot >= AGENT_SLOTS) {
		return AGENT_EMPTY;
	}

	k_mutex_lock(&lock, K_FOREVER);
	old = agents[slot].status;
	agents[slot].status = status;
	if (label != NULL) {
		strncpy(agents[slot].label, label, AGENT_LABEL_LEN - 1);
		agents[slot].label[AGENT_LABEL_LEN - 1] = '\0';
	} else if (status == AGENT_EMPTY) {
		agents[slot].label[0] = '\0';
	}
	k_mutex_unlock(&lock);

	bump();

	return old;
}

void state_agent_get(int slot, struct agent *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	*out = agents[slot];
	k_mutex_unlock(&lock);
}

bool state_all_empty(void)
{
	bool empty = true;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < AGENT_SLOTS; i++) {
		if (agents[i].status != AGENT_EMPTY) {
			empty = false;
			break;
		}
	}
	k_mutex_unlock(&lock);

	return empty;
}

int state_layer(void)
{
	return layer;
}

void state_layer_next(void)
{
	layer = (layer + 1) % num_layers;
	bump();
}

void state_note_set(const char *fmt, ...)
{
	va_list ap;

	k_mutex_lock(&lock, K_FOREVER);
	va_start(ap, fmt);
	vsnprintf(note, sizeof(note), fmt, ap);
	va_end(ap);
	k_mutex_unlock(&lock);

	bump();
}

void state_arm(const char *name)
{
	k_mutex_lock(&lock, K_FOREVER);
	armed = true;
	snprintf(note, sizeof(note), "%s?", name);
	k_mutex_unlock(&lock);

	bump();
}

void state_disarm(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	armed = false;
	k_mutex_unlock(&lock);

	bump();
}

bool state_armed(void)
{
	bool ret;

	k_mutex_lock(&lock, K_FOREVER);
	ret = armed;
	k_mutex_unlock(&lock);

	return ret;
}

void state_note_get(char *out, size_t len)
{
	k_mutex_lock(&lock, K_FOREVER);
	strncpy(out, note, len - 1);
	out[len - 1] = '\0';
	k_mutex_unlock(&lock);
}

uint32_t state_seq(void)
{
	return (uint32_t)atomic_get(&seq);
}
