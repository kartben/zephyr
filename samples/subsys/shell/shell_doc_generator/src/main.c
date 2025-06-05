#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/device.h>
#include <string.h>

/* Forward declaration from Zephyr shell internals */
const struct shell_static_entry *z_shell_cmd_get(const struct shell_static_entry *parent,
						 size_t idx, struct shell_static_entry *dloc);

TYPE_SECTION_START_EXTERN(union shell_cmd_entry, shell_root_cmds);
TYPE_SECTION_END_EXTERN(union shell_cmd_entry, shell_root_cmds);

// Increased for deep command trees
#define MAX_CMD_DEPTH 16

/**
 * @brief Check if a shell entry represents device names that should be filtered out
 *
 * This function uses the shell device APIs to determine if a shell command entry
 * represents dynamically generated device names (from shell_device_filter calls)
 * rather than actual subcommands.
 *
 * @param entry The shell static entry to check
 * @return true if this entry represents device names and should be filtered out
 */
static bool is_device_name_entry(const struct shell_static_entry *entry)
{
	// Check if it matches an actual device in the system
	const struct device *dev_list;
	size_t dev_count = z_device_get_all_static(&dev_list);
	for (size_t j = 0; j < dev_count; j++) {
		const struct device *dev = &dev_list[j];
		if (dev->name && strcmp(dev->name, entry->syntax) == 0) {
			return true;
		}
	}

	return false;
}

// Portable, safe path builder
static void build_cmd_path(char *buf, size_t buflen, const char *path[], int level)
{
	buf[0] = '\0';
	for (int i = 0; i < level; ++i) {
		strncat(buf, path[i], buflen - strlen(buf) - 1);
		if (i < level - 1) {
			strncat(buf, " ", buflen - strlen(buf) - 1);
		}
	}
}

/**
 * @brief Print YAML-safe string, escaping quotes and handling multilines
 */
static void print_yaml_string(const char *str, int indent)
{
	if (!str || *str == '\0') {
		return;
	}

	// Check if string contains newlines or special characters that need literal block
	bool has_newlines = strchr(str, '\n') != NULL;
	bool has_quotes = strchr(str, '"') != NULL || strchr(str, '\'') != NULL;

	if (has_newlines) {
		printk("|\n");
		// Split string by lines and add proper indentation
		char temp_str[1024];
		strncpy(temp_str, str, sizeof(temp_str) - 1);
		temp_str[sizeof(temp_str) - 1] = '\0';

		char *line = strtok(temp_str, "\n");
		while (line != NULL) {
			for (int i = 0; i < indent + 2; i++) {
				printk(" ");
			}
			printk("%s\n", line);
			line = strtok(NULL, "\n");
		}
	} else if (has_quotes) {
		// Use literal string with single quotes, escape single quotes
		printk("'");
		while (*str) {
			if (*str == '\'') {
				printk("''");
			} else {
				printk("%c", *str);
			}
			str++;
		}
		printk("'\n");
	} else {
		// Simple string
		printk("%s\n", str);
	}
}

/**
 * @brief Print shell command tree in YAML format
 *
 * Recursively prints shell commands and their subcommands, filtering out
 * device name entries to create clean documentation.
 *
 * @param cmd The shell command entry to print
 * @param level The nesting level (0 = root command)
 * @param path Array of command path components
 * @param path_len Current path length
 */
static void print_cmd_tree_yaml(const struct shell_static_entry *cmd, int level, const char *path[],
				int path_len)
{
	if (!cmd || !cmd->syntax || cmd->syntax[0] == '\0') {
		return;
	}
	if (path_len >= MAX_CMD_DEPTH) {
		printk("# ERROR: Command tree too deep at '%s', increase MAX_CMD_DEPTH!\n",
		       cmd->syntax);
		return;
	}

	char full_cmd[256];
	path[path_len] = cmd->syntax;
	build_cmd_path(full_cmd, sizeof(full_cmd), path, path_len + 1);

	// Print proper YAML indentation
	for (int i = 0; i < level; i++) {
		printk("  ");
	}

	if (level == 0) {
		printk("- name: %s\n", cmd->syntax);
	} else {
		printk("- name: %s\n", cmd->syntax);
	}

	// Add command field
	for (int i = 0; i < level; i++) {
		printk("  ");
	}
	printk("  command: %s\n", full_cmd);

	if (cmd->help) {
		const char *help = cmd->help;

		/* Check if this is structured help */
		if (shell_help_is_structured(help)) {
			const struct shell_cmd_help *structured =
				(const struct shell_cmd_help *)help;

			if (structured->description) {
				for (int i = 0; i < level; i++) {
					printk("  ");
				}
				printk("  description: ");
				print_yaml_string(structured->description, level * 2 + 2);
			}

			if (structured->usage) {
				for (int i = 0; i < level; i++) {
					printk("  ");
				}
				printk("  usage: ");
				print_yaml_string(structured->usage, level * 2 + 2);
			}
		} else {
			/* Handle original freeform help text */
			for (int i = 0; i < level; i++) {
				printk("  ");
			}
			printk("  help: ");
			print_yaml_string(help, level * 2 + 2);
		}
	}

	struct shell_static_entry dloc;
	size_t idx = 0;
	bool found_device_entries = false;
	bool has_subcommands = false;

	// First pass: check if we have any non-device subcommands
	while (1) {
		const struct shell_static_entry *sub = z_shell_cmd_get(cmd, idx++, &dloc);
		if (!sub) {
			break;
		}

		if (is_device_name_entry(sub)) {
			found_device_entries = true;
			continue;
		}
		has_subcommands = true;
		break;
	}

	if (has_subcommands) {
		for (int i = 0; i < level; i++) {
			printk("  ");
		}
		printk("  subcommands:\n");

		// Second pass: print the subcommands
		idx = 0;
		while (1) {
			const struct shell_static_entry *sub = z_shell_cmd_get(cmd, idx++, &dloc);
			if (!sub) {
				break;
			}

			// Skip device name entries - this is the key improvement
			if (is_device_name_entry(sub)) {
				continue;
			}

			print_cmd_tree_yaml(sub, level + 2, path, path_len + 1);
		}
	}

	// Add a note if we found and skipped device entries
	if (found_device_entries) {
		for (int i = 0; i < level; i++) {
			printk("  ");
		}
		printk("  accepts_device_names: true\n");
	}
}

static int shell_doc_generator_init(void)
{
	printk("# Zephyr Shell Command Reference\n");
	printk("# This file documents all available shell commands and subcommands in YAML "
	       "format\n");
	printk("# Device-specific arguments have been filtered out for clarity\n\n");
	printk("commands:\n");

	const char *path[MAX_CMD_DEPTH] = {0};
	for (const union shell_cmd_entry *cmd = TYPE_SECTION_START(shell_root_cmds);
	     cmd < TYPE_SECTION_END(shell_root_cmds); ++cmd) {
		print_cmd_tree_yaml(cmd->entry, 1, path, 0);
	}
	return 0;
}

SYS_INIT(shell_doc_generator_init, APPLICATION, 99);
