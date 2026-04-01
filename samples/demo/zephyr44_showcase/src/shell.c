/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — interactive shell commands
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Zephyr 4.4 feature showcased in this file                             │
 * │                                                                         │
 * │  shell_readline()  (include/zephyr/shell/shell.h)                      │
 * │                                                                         │
 * │  New in 4.4: a blocking readline call that pauses a shell command      │
 * │  handler until the user finishes typing a line (with optional          │
 * │  timeout).  Before 4.4 there was no clean way for a command handler   │
 * │  to prompt for a second input — workarounds involved separate state    │
 * │  machines or callback chains.  shell_readline() makes the handler      │
 * │  read like sequential code.                                            │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Commands exposed:
 *   securevault enroll <user_id>  — enroll a fingerprint (prompts for confirm)
 *   securevault verify <user_id>  — verify a specific user
 *   securevault identify          — identify whoever places their finger
 *   securevault status            — show current sensor readings
 *   securevault alarm             — simulate a high-temp alarm
 *   securevault wipe              — delete all templates (prompts for confirm)
 */

#include "auth.h"
#include "monitor.h"
#include "report.h"

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(sv_shell, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/**
 * Prompt the user for a yes/no answer using shell_readline() (new in 4.4).
 *
 * @return true if the user typed 'y' or 'Y', false otherwise.
 */
static bool confirm(const struct shell *sh, const char *prompt)
{
	uint8_t buf[8] = {0};

	shell_fprintf(sh, SHELL_WARNING, "%s [y/N]: ", prompt);

	/*
	 * [4.4 NEW] shell_readline() — block until the user presses Enter
	 * (or the 5-second timeout expires).
	 *
	 * Before 4.4: no supported mechanism for mid-command interactive
	 * input.  Developers had to split commands into multiple steps or
	 * implement custom input state machines.
	 */
	int ret = shell_readline(sh, buf, sizeof(buf), K_SECONDS(5));

	if (ret < 0) {
		shell_error(sh, "Input timeout or error (%d)", ret);
		return false;
	}

	return (buf[0] == 'y' || buf[0] == 'Y');
}

/* -------------------------------------------------------------------------
 * Command: securevault enroll <user_id>
 * ---------------------------------------------------------------------- */
static int cmd_enroll(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: securevault enroll <user_id>");
		return -EINVAL;
	}

	uint16_t user_id = (uint16_t)atoi(argv[1]);

	if (user_id == 0 || user_id > AUTH_MAX_USERS) {
		shell_error(sh, "user_id must be 1..%d", AUTH_MAX_USERS);
		return -EINVAL;
	}

	shell_print(sh, "Enrolling fingerprint for user %u", user_id);

	/* shell_readline() confirmation — the star of the show */
	if (!confirm(sh, "Place finger when ready and confirm")) {
		shell_warn(sh, "Enrollment cancelled");
		return 0;
	}

	int ret = auth_enroll(user_id, K_SECONDS(10));

	if (ret == 0) {
		shell_print(sh, "User %u enrolled successfully", user_id);
	} else {
		shell_error(sh, "Enrollment failed: %d", ret);
	}

	return ret;
}

/* -------------------------------------------------------------------------
 * Command: securevault verify <user_id>
 * ---------------------------------------------------------------------- */
static int cmd_verify(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: securevault verify <user_id>");
		return -EINVAL;
	}

	uint16_t user_id = (uint16_t)atoi(argv[1]);

	shell_print(sh, "Place finger for user %u…", user_id);

	int ret = auth_verify(user_id, K_SECONDS(5));

	if (ret == 0) {
		shell_print(sh, "Access GRANTED for user %u", user_id);
	} else if (ret == -ENOENT) {
		shell_warn(sh, "Access DENIED — fingerprint does not match");
	} else {
		shell_error(sh, "Verify error: %d", ret);
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * Command: securevault identify
 * ---------------------------------------------------------------------- */
static int cmd_identify(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Place finger for identification…");

	uint16_t matched_id = 0;
	int ret = auth_identify(&matched_id, K_SECONDS(5));

	if (ret == 0) {
		shell_print(sh, "Identified: user %u — Access GRANTED", matched_id);
	} else if (ret == -ENOENT) {
		shell_warn(sh, "Unknown fingerprint — Access DENIED");
	} else {
		shell_error(sh, "Identify error: %d", ret);
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * Command: securevault status
 * ---------------------------------------------------------------------- */
static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct monitor_reading r;

	monitor_get_latest(&r);

	shell_print(sh, "SecureVault Node — status");
	shell_print(sh, "  Sensor  : #%u | temp=%d.%d°C | hum=%d.%d%%",
		    r.seq,
		    r.temp_mc / 1000,  (r.temp_mc  % 1000) / 100,
		    r.hum_mpct / 1000, (r.hum_mpct % 1000) / 100);
	shell_print(sh, "  Auth DB : %zu user(s) enrolled", auth_enrolled_count());
	shell_print(sh, "  VPN     : %s",
#if defined(CONFIG_WIREGUARD)
		    "WireGuard enabled"
#else
		    "disabled (build with CONFIG_WIREGUARD=y)"
#endif
	);

	return 0;
}

/* -------------------------------------------------------------------------
 * Command: securevault alarm
 * ---------------------------------------------------------------------- */
static int cmd_alarm(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Simulating high-temperature alarm…");
	shell_print(sh, "(Watch the log: alarm_thread wakes → CPU freq scales UP)");
	monitor_trigger_alarm();
	k_sleep(K_MSEC(200));
	monitor_clear_alarm();

	return 0;
}

/* -------------------------------------------------------------------------
 * Command: securevault wipe
 * ---------------------------------------------------------------------- */
static int cmd_wipe(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!confirm(sh, "Delete ALL enrolled fingerprints? This cannot be undone")) {
		shell_warn(sh, "Wipe cancelled");
		return 0;
	}

	int ret = auth_wipe();

	if (ret == 0) {
		shell_print(sh, "All templates deleted");
	} else {
		shell_error(sh, "Wipe failed: %d", ret);
	}

	return ret;
}

/* -------------------------------------------------------------------------
 * Shell command registration
 * ---------------------------------------------------------------------- */
SHELL_STATIC_SUBCMD_SET_CREATE(sv_cmds,
	SHELL_CMD_ARG(enroll,   NULL, "Enroll a fingerprint. Usage: enroll <user_id>",
		      cmd_enroll,   2, 0),
	SHELL_CMD_ARG(verify,   NULL, "Verify a user. Usage: verify <user_id>",
		      cmd_verify,   2, 0),
	SHELL_CMD(identify, NULL, "Identify whoever places their finger", cmd_identify),
	SHELL_CMD(status,   NULL, "Show sensor readings and system status", cmd_status),
	SHELL_CMD(alarm,    NULL, "Simulate a high-temperature alarm",     cmd_alarm),
	SHELL_CMD(wipe,     NULL, "Delete all enrolled fingerprint templates", cmd_wipe),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(securevault, &sv_cmds,
		   "SecureVault Node commands (Zephyr 4.4 showcase)", NULL);
