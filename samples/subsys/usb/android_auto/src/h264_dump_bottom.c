/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Host side of the dump: plain libc file access, no Zephyr headers */

#include "h264_dump_bottom.h"

#include <fcntl.h>
#include <unistd.h>

int aa_dump_bottom_open(const char *path)
{
	return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

long aa_dump_bottom_write(int fd, const void *buf, unsigned long len)
{
	return (long)write(fd, buf, len);
}

void aa_dump_bottom_close(int fd)
{
	(void)close(fd);
}
