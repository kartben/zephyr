/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_ocpp_shell, LOG_LEVEL_INF);

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ocpp.h>
#include <zephyr/shell/shell.h>

#define OCPP_SHELL_MAX_SESSIONS 4

struct ocpp_shell_session {
	ocpp_session_handle_t handle;
	bool in_use;
};

static struct ocpp_shell_session sessions[OCPP_SHELL_MAX_SESSIONS];

static const char *ocpp_auth_status_str(enum ocpp_auth_status status)
{
	switch (status) {
	case OCPP_AUTH_INVALID:
		return "Invalid";
	case OCPP_AUTH_ACCEPTED:
		return "Accepted";
	case OCPP_AUTH_BLOCKED:
		return "Blocked";
	case OCPP_AUTH_EXPIRED:
		return "Expired";
	case OCPP_AUTH_CONCURRENT_TX:
		return "Concurrent Transaction";
	default:
		return "Unknown";
	}
}

static int find_free_session(void)
{
	for (int i = 0; i < OCPP_SHELL_MAX_SESSIONS; i++) {
		if (!sessions[i].in_use) {
			return i;
		}
	}
	return -1;
}

static int find_session_by_handle(ocpp_session_handle_t handle)
{
	for (int i = 0; i < OCPP_SHELL_MAX_SESSIONS; i++) {
		if (sessions[i].in_use && sessions[i].handle == handle) {
			return i;
		}
	}
	return -1;
}

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argv);

	if (argc > 1) {
		shell_error(sh, "Usage: ocpp info");
		return -EINVAL;
	}

	shell_print(sh, "OCPP Shell Info:");
	shell_print(sh, "  Max sessions: %d", OCPP_SHELL_MAX_SESSIONS);

	int active_sessions = 0;

	for (int i = 0; i < OCPP_SHELL_MAX_SESSIONS; i++) {
		if (sessions[i].in_use) {
			active_sessions++;
		}
	}

	shell_print(sh, "  Active sessions: %d", active_sessions);

	if (active_sessions > 0) {
		shell_print(sh, "\nActive Sessions:");
		for (int i = 0; i < OCPP_SHELL_MAX_SESSIONS; i++) {
			if (sessions[i].in_use) {
				shell_print(sh, "  Session %d: handle=%p", i,
					    sessions[i].handle);
			}
		}
	}

	return 0;
}

static int cmd_session_open(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	int idx;
	ocpp_session_handle_t handle;

	ARG_UNUSED(argv);

	if (argc > 1) {
		shell_error(sh, "Usage: ocpp session open");
		return -EINVAL;
	}

	idx = find_free_session();
	if (idx < 0) {
		shell_error(sh, "No free session slots available");
		return -ENOMEM;
	}

	ret = ocpp_session_open(&handle);
	if (ret < 0) {
		shell_error(sh, "Failed to open session: %d", ret);
		return ret;
	}

	sessions[idx].handle = handle;
	sessions[idx].in_use = true;

	shell_print(sh, "Session opened: index=%d, handle=%p", idx, handle);

	return 0;
}

static int cmd_session_close(const struct shell *sh, size_t argc, char **argv)
{
	int idx;
	long session_idx;
	char *endptr;

	if (argc != 2) {
		shell_error(sh, "Usage: ocpp session close <session_index>");
		return -EINVAL;
	}

	session_idx = strtol(argv[1], &endptr, 10);
	if (*endptr != '\0' || session_idx < 0 || session_idx >= OCPP_SHELL_MAX_SESSIONS) {
		shell_error(sh, "Invalid session index");
		return -EINVAL;
	}

	idx = session_idx;

	if (!sessions[idx].in_use) {
		shell_error(sh, "Session %d is not active", idx);
		return -EINVAL;
	}

	ocpp_session_close(sessions[idx].handle);
	sessions[idx].in_use = false;
	sessions[idx].handle = NULL;

	shell_print(sh, "Session %d closed", idx);

	return 0;
}

static int cmd_authorize(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	int idx;
	long session_idx;
	long timeout;
	char *endptr;
	enum ocpp_auth_status status;

	if (argc != 4) {
		shell_error(sh, "Usage: ocpp authorize <session_index> <idtag> <timeout_ms>");
		return -EINVAL;
	}

	session_idx = strtol(argv[1], &endptr, 10);
	if (*endptr != '\0' || session_idx < 0 || session_idx >= OCPP_SHELL_MAX_SESSIONS) {
		shell_error(sh, "Invalid session index");
		return -EINVAL;
	}

	timeout = strtol(argv[3], &endptr, 10);
	if (*endptr != '\0' || timeout < 0) {
		shell_error(sh, "Invalid timeout");
		return -EINVAL;
	}

	idx = session_idx;

	if (!sessions[idx].in_use) {
		shell_error(sh, "Session %d is not active", idx);
		return -EINVAL;
	}

	shell_print(sh, "Authorizing idtag '%s' on session %d...", argv[2], idx);

	ret = ocpp_authorize(sessions[idx].handle, argv[2], &status, (uint32_t)timeout);
	if (ret < 0) {
		shell_error(sh, "Authorization failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Authorization status: %s (%d)", ocpp_auth_status_str(status), status);

	return 0;
}

static int cmd_start_transaction(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	int idx;
	long session_idx;
	long meter_val;
	long conn_id;
	long timeout;
	char *endptr;

	if (argc != 5) {
		shell_error(sh, "Usage: ocpp start <session_index> <meter_val> "
				"<connector_id> <timeout_ms>");
		return -EINVAL;
	}

	session_idx = strtol(argv[1], &endptr, 10);
	if (*endptr != '\0' || session_idx < 0 || session_idx >= OCPP_SHELL_MAX_SESSIONS) {
		shell_error(sh, "Invalid session index");
		return -EINVAL;
	}

	meter_val = strtol(argv[2], &endptr, 10);
	if (*endptr != '\0' || meter_val < 0) {
		shell_error(sh, "Invalid meter value");
		return -EINVAL;
	}

	conn_id = strtol(argv[3], &endptr, 10);
	if (*endptr != '\0' || conn_id <= 0 || conn_id > 255) {
		shell_error(sh, "Invalid connector ID (must be 1-255)");
		return -EINVAL;
	}

	timeout = strtol(argv[4], &endptr, 10);
	if (*endptr != '\0' || timeout < 0) {
		shell_error(sh, "Invalid timeout");
		return -EINVAL;
	}

	idx = session_idx;

	if (!sessions[idx].in_use) {
		shell_error(sh, "Session %d is not active", idx);
		return -EINVAL;
	}

	shell_print(sh, "Starting transaction on session %d, connector %ld...",
		    idx, conn_id);

	ret = ocpp_start_transaction(sessions[idx].handle, (int)meter_val,
				     (uint8_t)conn_id, (uint32_t)timeout);
	if (ret < 0) {
		shell_error(sh, "Start transaction failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Transaction started successfully");

	return 0;
}

static int cmd_stop_transaction(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	int idx;
	long session_idx;
	long meter_val;
	long timeout;
	char *endptr;

	if (argc != 4) {
		shell_error(sh, "Usage: ocpp stop <session_index> <meter_val> <timeout_ms>");
		return -EINVAL;
	}

	session_idx = strtol(argv[1], &endptr, 10);
	if (*endptr != '\0' || session_idx < 0 || session_idx >= OCPP_SHELL_MAX_SESSIONS) {
		shell_error(sh, "Invalid session index");
		return -EINVAL;
	}

	meter_val = strtol(argv[2], &endptr, 10);
	if (*endptr != '\0' || meter_val < 0) {
		shell_error(sh, "Invalid meter value");
		return -EINVAL;
	}

	timeout = strtol(argv[3], &endptr, 10);
	if (*endptr != '\0' || timeout < 0) {
		shell_error(sh, "Invalid timeout");
		return -EINVAL;
	}

	idx = session_idx;

	if (!sessions[idx].in_use) {
		shell_error(sh, "Session %d is not active", idx);
		return -EINVAL;
	}

	shell_print(sh, "Stopping transaction on session %d...", idx);

	ret = ocpp_stop_transaction(sessions[idx].handle, (int)meter_val, (uint32_t)timeout);
	if (ret < 0) {
		shell_error(sh, "Stop transaction failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Transaction stopped successfully");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ocpp_session,
	SHELL_CMD(open, NULL, "Open a new OCPP session", cmd_session_open),
	SHELL_CMD(close, NULL, "Close an OCPP session\n"
			       "Usage: ocpp session close <session_index>",
		  cmd_session_close),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ocpp,
	SHELL_CMD(info, NULL, "Show OCPP information and status", cmd_info),
	SHELL_CMD(session, &sub_ocpp_session, "OCPP session management", NULL),
	SHELL_CMD(authorize, NULL,
		  "Authorize an ID tag\n"
		  "Usage: ocpp authorize <session_index> <idtag> <timeout_ms>",
		  cmd_authorize),
	SHELL_CMD(start, NULL,
		  "Start a transaction\n"
		  "Usage: ocpp start <session_index> <meter_val> <connector_id> <timeout_ms>",
		  cmd_start_transaction),
	SHELL_CMD(stop, NULL,
		  "Stop a transaction\n"
		  "Usage: ocpp stop <session_index> <meter_val> <timeout_ms>",
		  cmd_stop_transaction),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ocpp, &sub_ocpp, "OCPP shell commands", cmd_info);
