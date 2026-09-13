/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Host side of the framebuffer dump: plain libc, no Zephyr headers */

#include "hu_fb_dump_bottom.h"

#include <fcntl.h>
#include <unistd.h>

int hu_fb_dump_bottom_open(const char *path)
{
	return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

long hu_fb_dump_bottom_write(int fd, const void *buf, unsigned long len)
{
	return (long)write(fd, buf, len);
}
