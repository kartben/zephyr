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
 * @brief Print shell command tree in Markdown format
 *
 * Recursively prints shell commands and their subcommands, filtering out
 * device name entries to create clean documentation.
 *
 * @param cmd The shell command entry to print
 * @param level The nesting level (0 = root command)
 * @param path Array of command path components
 * @param path_len Current path length
 */
static void print_cmd_tree_md(const struct shell_static_entry *cmd, int level, const char *path[],
			      int path_len)
{
	if (!cmd || !cmd->syntax || cmd->syntax[0] == '\0') {
		return;
	}
	if (path_len >= MAX_CMD_DEPTH) {
		printk("\n**ERROR:** Command tree too deep at '%s', increase MAX_CMD_DEPTH!\n",
		       cmd->syntax);
		return;
	}

	char full_cmd[256];
	path[path_len] = cmd->syntax;
	build_cmd_path(full_cmd, sizeof(full_cmd), path, path_len + 1);

	if (level == 0) {
		printk("\n---\n\n");
		printk("## %s\n\n", cmd->syntax);
		printk("**Command:** `%s`\n\n", full_cmd);
	} else if (level == 1) {
		printk("### %s\n\n", cmd->syntax);
		printk("**Command:** `%s`\n\n", full_cmd);
	} else if (level == 2) {
		printk("#### %s\n\n", cmd->syntax);
		printk("**Command:** `%s`\n\n", full_cmd);
	} else {
		for (int i = 2; i < level; ++i) {
			printk("  ");
		}
		printk("- `%s`\n", full_cmd);
	}

	if (cmd->help) {
		const char *help = cmd->help;
		while (*help) {
			// Find the end of the current paragraph or help text
			const char *para_end = strstr(help, "\n\n");
			int para_len = para_end ? (para_end - help) : strlen(help);

			// Copy paragraph into a buffer for processing
			char para_buf[512];
			if (para_len >= (int)sizeof(para_buf)) {
				para_len = sizeof(para_buf) - 1;
			}
			memcpy(para_buf, help, para_len);
			para_buf[para_len] = '\0';

			// Process each line, wrapping blocks starting with 'Usage:'
			char *line = para_buf;
			bool in_code_block = false;
			while (line && *line) {
				// Find end of line
				char *next_line = strchr(line, '\n');
				if (next_line) {
					*next_line = '\0';
				}

				// Check for blank line
				bool is_blank = (*line == '\0');

				if (strncmp(line, "Usage:", 6) == 0) {
					if (!in_code_block) {
						for (int i = 0; i < (level > 2 ? level : 0); ++i) {
							printk("  ");
						}
						printk("```\n");
						in_code_block = true;
					}
					for (int i = 0; i < (level > 2 ? level : 0); ++i) {
						printk("  ");
					}
					printk("%s\n", line);
				} else if (in_code_block) {
					if (is_blank) {
						for (int i = 0; i < (level > 2 ? level : 0); ++i) {
							printk("  ");
						}
						printk("```\n\n");
						in_code_block = false;
					} else {
						for (int i = 0; i < (level > 2 ? level : 0); ++i) {
							printk("  ");
						}
						printk("%s\n", line);
					}
				} else {
					for (int i = 0; i < (level > 2 ? level : 0); ++i) {
						printk("  ");
					}
					printk("%s\n", line);
				}

				if (next_line) {
					line = next_line + 1;
				} else {
					break;
				}
			}
			if (in_code_block) {
				for (int i = 0; i < (level > 2 ? level : 0); ++i) {
					printk("  ");
				}
				printk("```\n\n");
			}
			help = para_end ? para_end + 2 : help + strlen(help);
		}
	}

	struct shell_static_entry dloc;
	size_t idx = 0;
	bool found_device_entries = false;

	while (1) {
		const struct shell_static_entry *sub = z_shell_cmd_get(cmd, idx++, &dloc);
		if (!sub) {
			break;
		}

		// Skip device name entries - this is the key improvement
		if (is_device_name_entry(sub)) {
			found_device_entries = true;
			continue;
		}

		print_cmd_tree_md(sub, level + 1, path, path_len + 1);
	}

	// Add a note if we found and skipped device entries
	if (found_device_entries) {
		// for (int i = 0; i < (level > 1 ? level : 0); ++i) {
		// 	printk("  ");
		// }
		// printk("\n*Note: This command accepts device names as arguments. ");
		// printk("Use `device list` to see available devices.*\n\n");
	}
}

static int shell_doc_generator_init(void)
{
	printk("# Zephyr Shell Command Reference\n\n");
	printk("This page documents all available shell commands and subcommands, including their "
	       "usage and help text. Device-specific arguments have been filtered out for "
	       "clarity.\n\n");

	const char *path[MAX_CMD_DEPTH] = {0};
	for (const union shell_cmd_entry *cmd = TYPE_SECTION_START(shell_root_cmds);
	     cmd < TYPE_SECTION_END(shell_root_cmds); ++cmd) {
		print_cmd_tree_md(cmd->entry, 0, path, 0);
	}
	return 0;
}

SYS_INIT(shell_doc_generator_init, APPLICATION, 99);
