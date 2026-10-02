/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ESP-DL can load models from an SD card through fopen(), which the C
 * library implements on top of these POSIX calls. Models are embedded in the
 * application here, so without a file system the calls fail. A file system
 * providing them takes precedence over these weak definitions.
 */

#include <errno.h>
#include <sys/types.h>

#include <zephyr/toolchain.h>

__weak int open(const char *name, int flags, ...)
{
	ARG_UNUSED(name);
	ARG_UNUSED(flags);
	errno = ENOSYS;

	return -1;
}

__weak int close(int fd)
{
	ARG_UNUSED(fd);
	errno = ENOSYS;

	return -1;
}

__weak ssize_t read(int fd, void *buf, size_t count)
{
	ARG_UNUSED(fd);
	ARG_UNUSED(buf);
	ARG_UNUSED(count);
	errno = ENOSYS;

	return -1;
}

__weak ssize_t write(int fd, const void *buf, size_t count)
{
	ARG_UNUSED(fd);
	ARG_UNUSED(buf);
	ARG_UNUSED(count);
	errno = ENOSYS;

	return -1;
}

__weak off_t lseek(int fd, off_t offset, int whence)
{
	ARG_UNUSED(fd);
	ARG_UNUSED(offset);
	ARG_UNUSED(whence);
	errno = ENOSYS;

	return -1;
}
