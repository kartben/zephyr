/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(link, LOG_LEVEL_INF);

#include "agent_pad.h"

#define LINE_LEN 64

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

static char rx_line[LINE_LEN];
static size_t rx_len;

K_MSGQ_DEFINE(line_msgq, LINE_LEN, 4, 4);

static void link_send(const char *line)
{
	uint32_t dtr = 0;

	uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
	if (dtr == 0U) {
		return;
	}

	while (*line != '\0') {
		uart_poll_out(uart_dev, *line++);
	}
	uart_poll_out(uart_dev, '\n');
}

void link_notify_keypress(int slot)
{
	char line[16];

	snprintf(line, sizeof(line), "key %d", slot + 1);
	link_send(line);
}

static bool parse_status(const char *word, enum agent_status *status)
{
	static const struct {
		const char *word;
		enum agent_status status;
	} map[] = {
		{"idle", AGENT_IDLE},	  {"thinking", AGENT_THINKING},
		{"busy", AGENT_THINKING}, {"done", AGENT_DONE},
		{"input", AGENT_INPUT},	  {"error", AGENT_ERROR},
	};

	for (size_t i = 0; i < ARRAY_SIZE(map); i++) {
		if (strcmp(word, map[i].word) == 0) {
			*status = map[i].status;
			return true;
		}
	}

	return false;
}

static void status_effects(int slot, enum agent_status old, enum agent_status status)
{
	if (status == old) {
		return;
	}

	switch (status) {
	case AGENT_DONE:
		state_note_set("A%d done", slot + 1);
		chime_play(CHIME_DONE);
		break;
	case AGENT_INPUT:
		state_note_set("A%d needs you", slot + 1);
		chime_play(CHIME_INPUT);
		break;
	case AGENT_ERROR:
		state_note_set("A%d error", slot + 1);
		chime_play(CHIME_ERROR);
		break;
	case AGENT_THINKING:
		state_note_set("A%d thinking", slot + 1);
		break;
	default:
		break;
	}
}

static void handle_line(char *line)
{
	char *saveptr;
	char *cmd = strtok_r(line, " ", &saveptr);

	if (cmd == NULL) {
		return;
	}

	if (strcmp(cmd, "agent") == 0) {
		char *slot_str = strtok_r(NULL, " ", &saveptr);
		char *status_str = strtok_r(NULL, " ", &saveptr);
		char *label = strtok_r(NULL, "", &saveptr);
		enum agent_status status;
		int slot;

		if (slot_str == NULL || status_str == NULL) {
			return;
		}

		slot = atoi(slot_str) - 1;
		if (slot < 0 || slot >= AGENT_SLOTS || !parse_status(status_str, &status)) {
			return;
		}

		status_effects(slot, state_agent_set(slot, status, label), status);
	} else if (strcmp(cmd, "clear") == 0) {
		char *slot_str = strtok_r(NULL, " ", &saveptr);

		if (slot_str == NULL || strcmp(slot_str, "all") == 0) {
			for (int i = 0; i < AGENT_SLOTS; i++) {
				state_agent_set(i, AGENT_EMPTY, NULL);
			}
		} else {
			state_agent_set(atoi(slot_str) - 1, AGENT_EMPTY, NULL);
		}
	} else if (strcmp(cmd, "ping") == 0) {
		link_send("pong");
	}
}

static void line_work_handler(struct k_work *work)
{
	char line[LINE_LEN];

	while (k_msgq_get(&line_msgq, line, K_NO_WAIT) == 0) {
		handle_line(line);
	}
}

static K_WORK_DEFINE(line_work, line_work_handler);

static void uart_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	uart_irq_update(dev);

	while (uart_irq_rx_ready(dev) && uart_fifo_read(dev, &c, 1) == 1) {
		if (c == '\n' || c == '\r') {
			if (rx_len > 0) {
				rx_line[rx_len] = '\0';
				k_msgq_put(&line_msgq, rx_line, K_NO_WAIT);
				k_work_submit(&line_work);
				rx_len = 0;
			}
		} else if (rx_len < LINE_LEN - 1) {
			rx_line[rx_len++] = c;
		}
	}
}

int link_init(void)
{
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("CDC ACM device not ready");
		return -EIO;
	}

	uart_irq_callback_set(uart_dev, uart_cb);
	uart_irq_rx_enable(uart_dev);

	return 0;
}
