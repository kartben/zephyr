#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Bridge a real serial device (e.g. USB-serial adapter) to a PTY that QEMU can
# use.  This ensures the host serial port is opened at the correct baud rate,
# working around QEMU versions that do not propagate the guest UART baud rate to
# the host device.
#
# Usage:
#   python3 serial_bridge.py /dev/tty.usbserial-XXXX [baud]
#
# Then point QEMU at the PTY printed to stderr:
#   CONFIG_QEMU_EXTRA_FLAGS="-serial /dev/ttysNNN"

import fcntl
import os
import select
import sys
import termios
import tty


def open_serial(path, baud=115200):
    """Open a serial device at the given baud rate in raw mode."""
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    # Clear NONBLOCK after open so reads can block in select()
    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)

    attrs = termios.tcgetattr(fd)
    # Raw mode
    attrs[0] = 0                                          # iflag
    attrs[1] = 0                                          # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
    attrs[3] = 0                                          # lflag
    attrs[6][termios.VMIN] = 1
    attrs[6][termios.VTIME] = 0

    baud_const = getattr(termios, f"B{baud}", None)
    if baud_const is None:
        print(f"error: unsupported baud rate {baud}", file=sys.stderr)
        sys.exit(1)
    attrs[4] = baud_const   # ispeed
    attrs[5] = baud_const   # ospeed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)

    # Verify
    check = termios.tcgetattr(fd)
    if check[4] != baud_const or check[5] != baud_const:
        print(f"warning: baud rate verification failed", file=sys.stderr)
    return fd


def open_pty():
    """Open a PTY master/slave pair, returning (master_fd, slave_path)."""
    master, slave = os.openpty()
    tty.setraw(master)
    tty.setraw(slave)
    slave_path = os.ttyname(slave)
    os.close(slave)  # QEMU will open the slave side
    return master, slave_path


def bridge(serial_fd, pty_fd):
    """Bidirectional relay between serial_fd and pty_fd."""
    # Both fds need O_NONBLOCK for select-based I/O; EIO from the PTY master
    # is normal when the slave side is not yet open (macOS / Linux).
    for fd in (serial_fd, pty_fd):
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    try:
        while True:
            r, _, _ = select.select([serial_fd, pty_fd], [], [], 1.0)
            for fd in r:
                try:
                    data = os.read(fd, 4096)
                except OSError as e:
                    import errno
                    if e.errno in (errno.EIO, errno.EAGAIN):
                        continue  # PTY slave not open yet, or no data
                    raise
                if not data:
                    continue  # Empty read — slave not attached
                dst = pty_fd if fd == serial_fd else serial_fd
                try:
                    os.write(dst, data)
                except OSError:
                    pass  # Drop if destination not ready
    except KeyboardInterrupt:
        pass


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} /dev/ttyXXX [baud]", file=sys.stderr)
        sys.exit(1)

    dev = sys.argv[1]
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    serial_fd = open_serial(dev, baud)
    pty_fd, pty_path = open_pty()

    print(f"serial:  {dev} @ {baud}", file=sys.stderr)
    print(f"pty:     {pty_path}", file=sys.stderr)
    print(f"Use:     CONFIG_QEMU_EXTRA_FLAGS=\"-serial {pty_path}\"", file=sys.stderr)
    print(pty_path)  # stdout: just the path, for scripting
    sys.stdout.flush()

    bridge(serial_fd, pty_fd)

    os.close(serial_fd)
    os.close(pty_fd)


if __name__ == "__main__":
    main()
