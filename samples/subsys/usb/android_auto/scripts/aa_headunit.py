#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Virtual Android Auto head unit for the Zephyr Android Auto accessory sample.

Connects to the sample over TCP or USB (accessory mode), performs the version
exchange, the TLS handshake and service discovery, opens a video channel and an
input channel, decodes the H.264 stream and shows it in a window whose mouse
events are sent back as touch events.

Dependencies beyond the standard library are imported on demand: pyusb for
``--usb``, PyAV, numpy and pygame for the video window.
"""

import argparse
import contextlib
import logging
import queue
import socket
import ssl
import struct
import sys
import threading
import time

LOG = logging.getLogger("aa_headunit")

# Frame flags
FRAME_FIRST = 0x01
FRAME_LAST = 0x02
FRAME_BULK = FRAME_FIRST | FRAME_LAST
FRAME_CONTROL = 0x04
FRAME_ENCRYPTED = 0x08

CHANNEL_CONTROL = 0
CHANNEL_VIDEO = 1
CHANNEL_INPUT = 2
CHANNEL_SENSOR = 3

# Control channel message ids
VERSION_REQUEST = 0x0001
VERSION_RESPONSE = 0x0002
SSL_HANDSHAKE = 0x0003
AUTH_COMPLETE = 0x0004
SERVICE_DISCOVERY_REQUEST = 0x0005
SERVICE_DISCOVERY_RESPONSE = 0x0006
CHANNEL_OPEN_REQUEST = 0x0007
CHANNEL_OPEN_RESPONSE = 0x0008
PING_REQUEST = 0x000B
PING_RESPONSE = 0x000C
NAVIGATION_FOCUS_REQUEST = 0x000D
NAVIGATION_FOCUS_RESPONSE = 0x000E
SHUTDOWN_REQUEST = 0x000F
SHUTDOWN_RESPONSE = 0x0010

# Video channel message ids
MEDIA_WITH_TIMESTAMP = 0x0000
MEDIA_INDICATION = 0x0001
AV_SETUP_REQUEST = 0x8000
AV_START_INDICATION = 0x8001
AV_SETUP_RESPONSE = 0x8003
AV_MEDIA_ACK = 0x8004
VIDEO_FOCUS_REQUEST = 0x8007
VIDEO_FOCUS_INDICATION = 0x8008

# Input channel message ids
INPUT_EVENT_INDICATION = 0x8001
BINDING_REQUEST = 0x8002
BINDING_RESPONSE = 0x8003

# Sensor channel message ids
SENSOR_START_REQUEST = 0x8001
SENSOR_START_RESPONSE = 0x8002
SENSOR_EVENT_INDICATION = 0x8003

TOUCH_PRESS = 0
TOUCH_RELEASE = 1
TOUCH_DRAG = 2

RESOLUTIONS = {"800x480": 1, "1280x720": 2, "1920x1080": 3}
FPS_VALUES = {30: 1, 60: 2}

AOA_VID = 0x18D1
AOA_PIDS = (0x2D00, 0x2D01)
AOA_GET_PROTOCOL = 51
AOA_SEND_STRING = 52
AOA_START = 53
AOA_STRINGS = ["Android", "Android Auto", "Android Auto", "1.0", "", ""]

USB_READ_SIZE = 16384
HANDSHAKE_TIMEOUT_S = 15.0

WIRE_VARINT = 0
WIRE_LEN = 2


class ProtocolError(Exception):
    """Raised when the peer violates the protocol."""


# Minimal protobuf codec: enough for the handful of messages exchanged here.


def _varint(value):
    out = bytearray()
    value &= 0xFFFFFFFFFFFFFFFF
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def pb_encode(fields):
    """Encode [(number, value)] where value is int, bool, bytes/str or a nested list."""
    out = bytearray()
    for number, value in fields:
        if isinstance(value, bool):
            value = int(value)
        if isinstance(value, int):
            out += _varint((number << 3) | WIRE_VARINT) + _varint(value)
        else:
            if isinstance(value, list):
                value = pb_encode(value)
            elif isinstance(value, str):
                value = value.encode()
            out += _varint((number << 3) | WIRE_LEN) + _varint(len(value)) + value
    return bytes(out)


def pb_decode(data):
    """Decode into {number: [value, ...]}; nested messages stay as bytes."""
    fields = {}
    pos = 0

    def read_varint():
        nonlocal pos
        shift = 0
        result = 0
        while True:
            if pos >= len(data):
                raise ProtocolError("truncated protobuf")
            byte = data[pos]
            pos += 1
            result |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                return result

    while pos < len(data):
        tag = read_varint()
        number, wire = tag >> 3, tag & 7
        if wire == WIRE_VARINT:
            value = read_varint()
        elif wire == WIRE_LEN:
            length = read_varint()
            value = bytes(data[pos : pos + length])
            pos += length
        elif wire == 1:
            value = data[pos : pos + 8]
            pos += 8
        elif wire == 5:
            value = data[pos : pos + 4]
            pos += 4
        else:
            raise ProtocolError(f"unsupported wire type {wire}")
        fields.setdefault(number, []).append(value)
    return fields


def pb_first(fields, number, default=None):
    values = fields.get(number)
    return values[0] if values else default


# Transports


class TcpTransport:
    """Client connection to the sample listening on a TCP port."""

    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port))
        self.sock.settimeout(1.0)

    def read(self, size):
        try:
            return self.sock.recv(size)
        except TimeoutError:
            return b""

    def write(self, data):
        self.sock.sendall(data)

    def close(self):
        self.sock.close()


class UsbTransport:
    """Bulk pipes of a device in accessory mode."""

    def __init__(self, vid_pid=None, aoa=None):
        usb = self._load_pyusb()
        self.usb = usb
        dev = None
        if aoa is not None:
            dev = self._switch_to_accessory(usb, aoa)
        elif vid_pid is not None:
            dev = usb.core.find(idVendor=vid_pid[0], idProduct=vid_pid[1])
        else:
            for pid in AOA_PIDS:
                dev = usb.core.find(idVendor=AOA_VID, idProduct=pid)
                if dev is not None:
                    break
        if dev is None:
            raise ProtocolError("no accessory device found")
        self.dev = dev
        self._claim(usb, dev)

    @staticmethod
    def _load_pyusb():
        try:
            import usb.core  # noqa: F401
            import usb.util  # noqa: F401
        except ImportError:
            sys.exit("pyusb is required for USB: pip install pyusb (and libusb)")
        return sys.modules["usb"]

    def _switch_to_accessory(self, usb, vid_pid):
        dev = usb.core.find(idVendor=vid_pid[0], idProduct=vid_pid[1])
        if dev is None:
            raise ProtocolError(f"device {vid_pid[0]:04x}:{vid_pid[1]:04x} not found")
        version = dev.ctrl_transfer(0xC0, AOA_GET_PROTOCOL, 0, 0, 2)
        LOG.info("Accessory protocol version %d", struct.unpack("<H", bytes(version))[0])
        for index, text in enumerate(AOA_STRINGS):
            dev.ctrl_transfer(0x40, AOA_SEND_STRING, 0, index, text.encode() + b"\0")
        dev.ctrl_transfer(0x40, AOA_START, 0, 0, None)
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            time.sleep(0.2)
            for pid in AOA_PIDS:
                found = usb.core.find(idVendor=AOA_VID, idProduct=pid)
                if found is not None:
                    return found
        raise ProtocolError("device did not re-enumerate in accessory mode")

    def _claim(self, usb, dev):
        try:
            if dev.is_kernel_driver_active(0):
                dev.detach_kernel_driver(0)
        except (NotImplementedError, usb.core.USBError):
            pass
        dev.set_configuration()
        intf = dev.get_active_configuration()[(0, 0)]
        usb.util.claim_interface(dev, intf)
        self.ep_in = usb.util.find_descriptor(
            intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
            == usb.util.ENDPOINT_IN,
        )
        self.ep_out = usb.util.find_descriptor(
            intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
            == usb.util.ENDPOINT_OUT,
        )
        if self.ep_in is None or self.ep_out is None:
            raise ProtocolError("accessory interface has no bulk endpoints")
        LOG.info("Claimed %04x:%04x interface 0", dev.idVendor, dev.idProduct)

    def read(self, size):
        try:
            return bytes(self.ep_in.read(max(size, USB_READ_SIZE), timeout=1000))
        except self.usb.core.USBTimeoutError:
            return b""

    def write(self, data):
        self.ep_out.write(data, timeout=5000)

    def close(self):
        self.usb.util.dispose_resources(self.dev)


# Framing


class Framer:
    """Splits the byte stream into frames and reassembles fragmented messages."""

    def __init__(self):
        self.buf = bytearray()
        self.partial = {}

    @staticmethod
    def encode(channel, flags, payload, total=None):
        header = struct.pack(">BBH", channel, flags, len(payload))
        if (flags & FRAME_BULK) == FRAME_FIRST:
            header += struct.pack(">I", total if total is not None else len(payload))
        return header + payload

    def feed(self, data):
        """Yield (channel, flags, payload) for every complete message."""
        self.buf += data
        while True:
            if len(self.buf) < 4:
                return
            channel, flags, length = struct.unpack(">BBH", self.buf[:4])
            header_len = 4
            if (flags & FRAME_BULK) == FRAME_FIRST:
                if len(self.buf) < 8:
                    return
                header_len = 8
            if len(self.buf) < header_len + length:
                return
            payload = bytes(self.buf[header_len : header_len + length])
            del self.buf[: header_len + length]
            yield channel, flags, payload


# TLS


class TlsClient:
    """TLS 1.2 client whose records travel inside protocol messages."""

    def __init__(self, cert=None):
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2
        ctx.check_hostname = False
        if cert:
            ctx.verify_mode = ssl.CERT_REQUIRED
            ctx.load_verify_locations(cert)
        else:
            ctx.verify_mode = ssl.CERT_NONE
        self.incoming = ssl.MemoryBIO()
        self.outgoing = ssl.MemoryBIO()
        self.obj = ctx.wrap_bio(self.incoming, self.outgoing, server_side=False)
        self.done = False

    def handshake_step(self, data=b""):
        """Feed handshake bytes, return the records to send and whether it completed."""
        if data:
            self.incoming.write(data)
        try:
            self.obj.do_handshake()
            self.done = True
        except ssl.SSLWantReadError:
            pass
        return self.outgoing.read(), self.done

    def encrypt(self, plain):
        self.obj.write(plain)
        return self.outgoing.read()

    def decrypt(self, record):
        self.incoming.write(record)
        out = bytearray()
        while True:
            try:
                out += self.obj.read(65536)
            except ssl.SSLWantReadError:
                return bytes(out)


# Head unit


class HeadUnit:
    """Protocol state machine on top of a transport."""

    def __init__(self, transport, args, video_sink):
        self.transport = transport
        self.args = args
        self.video_sink = video_sink
        self.framer = Framer()
        self.tls = TlsClient(args.cert)
        self.encrypted = False
        self.lock = threading.Lock()
        self.session = 0
        self.running = True
        self.streaming = False
        self.last_ping = time.monotonic()
        self.ping_pending = False
        self.frames = 0
        self.bytes = 0
        self.window_start = time.monotonic()
        self.window_bytes = 0
        self.window_frames = 0
        self.partial = {}
        self.stats = ""

    # Sending

    def send_raw(self, channel, flags, payload, total=None):
        with self.lock:
            self.transport.write(self.framer.encode(channel, flags, payload, total))

    def send(self, channel, msg_id, body=b"", control=False):
        payload = struct.pack(">H", msg_id) + body
        flags = FRAME_BULK | (FRAME_CONTROL if control else 0)
        if self.encrypted:
            with self.lock:
                record = self.tls.encrypt(payload)
                self.transport.write(self.framer.encode(channel, flags | FRAME_ENCRYPTED, record))
        else:
            self.send_raw(channel, flags, payload)
        LOG.debug("TX ch %d msg 0x%04x len %d", channel, msg_id, len(body))

    # Receiving

    def handle_frame(self, channel, flags, payload):
        if flags & FRAME_ENCRYPTED:
            payload = self.tls.decrypt(payload)
        fragment = flags & FRAME_BULK
        if fragment == FRAME_BULK:
            message = payload
        else:
            if fragment == FRAME_FIRST:
                self.partial[channel] = bytearray()
            if channel not in self.partial:
                raise ProtocolError("fragment without a first frame")
            self.partial[channel] += payload
            if fragment != FRAME_LAST:
                return
            message = bytes(self.partial.pop(channel))
        if len(message) < 2:
            raise ProtocolError("short message")
        msg_id = struct.unpack(">H", message[:2])[0]
        body = message[2:]
        if channel == CHANNEL_CONTROL:
            self.handle_control(msg_id, body)
        elif msg_id == CHANNEL_OPEN_REQUEST:
            fields = pb_decode(body)
            LOG.info("Channel %d open request (priority %s)", channel, pb_first(fields, 1))
            self.send(channel, CHANNEL_OPEN_RESPONSE, pb_encode([(1, 0)]), control=True)
        elif channel == CHANNEL_VIDEO:
            self.handle_video(msg_id, body)
        elif channel == CHANNEL_INPUT:
            self.handle_input(msg_id, body)
        elif channel == CHANNEL_SENSOR:
            self.handle_sensor(msg_id, body)
        else:
            LOG.warning("Message 0x%04x on unknown channel %d", msg_id, channel)

    def handle_control(self, msg_id, body):
        if msg_id == VERSION_RESPONSE:
            major, minor, status = struct.unpack(">HHH", body[:6])
            LOG.info("Phone protocol version %d.%d, status %d", major, minor, status)
            if status != 0:
                raise ProtocolError("version mismatch")
            records, _ = self.tls.handshake_step()
            self.send(CHANNEL_CONTROL, SSL_HANDSHAKE, records)
        elif msg_id == SSL_HANDSHAKE:
            records, done = self.tls.handshake_step(body)
            if records:
                self.send(CHANNEL_CONTROL, SSL_HANDSHAKE, records)
            if done:
                LOG.info("TLS handshake done: %s %s", self.tls.obj.version(), self.tls.obj.cipher())
                self.send(CHANNEL_CONTROL, AUTH_COMPLETE, pb_encode([(1, 0)]))
                self.encrypted = True
        elif msg_id == SERVICE_DISCOVERY_REQUEST:
            fields = pb_decode(body)
            LOG.info(
                "Service discovery request from %s (%s)",
                pb_first(fields, 4, b"?").decode(errors="replace"),
                pb_first(fields, 5, b"?").decode(errors="replace"),
            )
            self.send(CHANNEL_CONTROL, SERVICE_DISCOVERY_RESPONSE, self.service_discovery())
        elif msg_id == PING_REQUEST:
            self.send(CHANNEL_CONTROL, PING_RESPONSE, body)
        elif msg_id == PING_RESPONSE:
            self.ping_pending = False
        elif msg_id == NAVIGATION_FOCUS_REQUEST:
            self.send(CHANNEL_CONTROL, NAVIGATION_FOCUS_RESPONSE, body)
        elif msg_id == SHUTDOWN_REQUEST:
            LOG.info("Phone requested shutdown")
            self.send(CHANNEL_CONTROL, SHUTDOWN_RESPONSE)
            self.running = False
        elif msg_id == SHUTDOWN_RESPONSE:
            self.running = False
        else:
            LOG.warning("Unhandled control message 0x%04x (%d bytes)", msg_id, len(body))

    def service_discovery(self):
        width, height = (int(v) for v in self.args.resolution.split("x"))
        video_config = [
            (1, RESOLUTIONS[self.args.resolution]),
            (2, FPS_VALUES[self.args.fps]),
            (3, 0),
            (4, 0),
            (5, 140),
        ]
        channels = [
            (1, [(1, CHANNEL_VIDEO), (3, [(1, 3), (4, video_config), (5, True)])]),
            (1, [(1, CHANNEL_INPUT), (4, [(2, [(1, width), (2, height)])])]),
        ]
        if self.args.sensors:
            channels.append((1, [(1, CHANNEL_SENSOR), (2, [(1, [(1, 10)]), (1, [(1, 13)])])]))
        return pb_encode(
            channels
            + [
                (2, "Zephyr virtual head unit"),
                (3, "Universal"),
                (4, "2026"),
                (5, "0001"),
                (6, True),
                (7, "Zephyr Project"),
                (8, "aa_headunit.py"),
                (9, "1"),
                (10, "1.0"),
                (11, False),
                (12, False),
            ]
        )

    def handle_video(self, msg_id, body):
        if msg_id == AV_SETUP_REQUEST:
            fields = pb_decode(body)
            LOG.info("Video setup request, config %s", pb_first(fields, 1))
            response = pb_encode([(1, 2), (2, self.args.max_unacked), (3, 0)])
            self.send(CHANNEL_VIDEO, AV_SETUP_RESPONSE, response)
            self.send(CHANNEL_VIDEO, VIDEO_FOCUS_INDICATION, pb_encode([(1, 1), (2, False)]))
        elif msg_id == AV_START_INDICATION:
            fields = pb_decode(body)
            self.session = pb_first(fields, 1, 0)
            LOG.info(
                "Video stream started, session %d config %s", self.session, pb_first(fields, 2)
            )
            self.streaming = True
        elif msg_id == VIDEO_FOCUS_REQUEST:
            fields = pb_decode(body)
            LOG.info("Video focus request mode %s", pb_first(fields, 2))
            self.send(CHANNEL_VIDEO, VIDEO_FOCUS_INDICATION, pb_encode([(1, 1), (2, False)]))
        elif msg_id in (MEDIA_WITH_TIMESTAMP, MEDIA_INDICATION):
            data = body[8:] if msg_id == MEDIA_WITH_TIMESTAMP else body
            self.send(CHANNEL_VIDEO, AV_MEDIA_ACK, pb_encode([(1, self.session), (2, 1)]))
            self.frames += 1
            self.bytes += len(data)
            self.window_frames += 1
            self.window_bytes += len(data)
            now = time.monotonic()
            elapsed = now - self.window_start
            if elapsed >= 1.0:
                self.stats = (
                    f"{self.window_frames / elapsed:.1f} fps  "
                    f"{self.window_bytes * 8 / elapsed / 1000:.0f} kbit/s"
                )
                LOG.info("Video: %s (%d frames, %d bytes)", self.stats, self.frames, self.bytes)
                self.window_start = now
                self.window_frames = 0
                self.window_bytes = 0
            self.video_sink(data)
        else:
            LOG.warning("Unhandled video message 0x%04x (%d bytes)", msg_id, len(body))

    def handle_input(self, msg_id, body):
        if msg_id == BINDING_REQUEST:
            fields = pb_decode(body)
            LOG.info("Key binding request for %s", fields.get(1, []))
            self.send(CHANNEL_INPUT, BINDING_RESPONSE, pb_encode([(1, 0)]))
        else:
            LOG.warning("Unhandled input message 0x%04x (%d bytes)", msg_id, len(body))

    def handle_sensor(self, msg_id, body):
        if msg_id == SENSOR_START_REQUEST:
            fields = pb_decode(body)
            LOG.info("Sensor start request, type %s", pb_first(fields, 1))
            self.send(CHANNEL_SENSOR, SENSOR_START_RESPONSE, pb_encode([(1, 0)]))
            self.send_night(False)
        else:
            LOG.warning("Unhandled sensor message 0x%04x (%d bytes)", msg_id, len(body))

    def send_night(self, night):
        if self.args.sensors and self.encrypted:
            self.send(CHANNEL_SENSOR, SENSOR_EVENT_INDICATION, pb_encode([(10, [(1, night)])]))

    def send_touch(self, x, y, action):
        if not self.encrypted:
            return
        touch = [(1, [(1, int(x)), (2, int(y)), (3, 0)]), (2, 0), (3, action)]
        body = pb_encode([(1, int(time.monotonic() * 1e9)), (2, 0), (3, touch)])
        self.send(CHANNEL_INPUT, INPUT_EVENT_INDICATION, body)

    def send_key(self, keycode, pressed):
        if not self.encrypted:
            return
        buttons = [(1, [(1, keycode), (2, pressed), (3, 0), (4, False)])]
        body = pb_encode([(1, int(time.monotonic() * 1e9)), (2, 0), (4, buttons)])
        self.send(CHANNEL_INPUT, INPUT_EVENT_INDICATION, body)

    def shutdown(self):
        if self.encrypted:
            with contextlib.suppress(OSError):
                self.send(CHANNEL_CONTROL, SHUTDOWN_REQUEST, pb_encode([(1, 0)]))
        self.running = False

    # Main loop

    def run(self):
        self.send_raw(CHANNEL_CONTROL, FRAME_BULK, struct.pack(">HHH", VERSION_REQUEST, 1, 1))
        start = time.monotonic()
        while self.running:
            data = self.transport.read(USB_READ_SIZE)
            if data:
                for channel, flags, payload in self.framer.feed(data):
                    self.handle_frame(channel, flags, payload)
            elif isinstance(self.transport, TcpTransport) and self.transport.sock.fileno() < 0:
                break
            now = time.monotonic()
            if not self.encrypted and now - start > HANDSHAKE_TIMEOUT_S:
                raise ProtocolError("session setup timed out")
            if self.encrypted and now - self.last_ping > self.args.ping_interval:
                if self.ping_pending:
                    LOG.warning("Phone did not answer the previous ping")
                self.ping_pending = True
                self.last_ping = now
                self.send(CHANNEL_CONTROL, PING_REQUEST, pb_encode([(1, int(now * 1e6))]))


# Video output


class VideoWindow:
    """Decodes the stream with PyAV and shows it with pygame; mouse becomes touch."""

    def __init__(self, resolution, dump, snapshot=None, snapshot_after=5.0):
        self.width, self.height = (int(v) for v in resolution.split("x"))
        self.dump = dump
        self.snapshot = snapshot
        self.snapshot_after = snapshot_after
        self.queue = queue.Queue(maxsize=8)
        self.codec = None
        self.pygame = None

    def start(self):
        try:
            import av
            import numpy  # noqa: F401
            import pygame
        except ImportError:
            sys.exit(
                "PyAV, numpy and pygame are required for the window: pip install av numpy pygame"
            )
        self.codec = av.CodecContext.create("h264", "r")
        self.pygame = pygame
        pygame.init()
        pygame.display.set_caption("Zephyr virtual head unit")
        self.screen = pygame.display.set_mode((self.width, self.height))

    def sink(self, data):
        if self.dump:
            self.dump.write(data)
        if self.codec is None:
            return
        for packet in self.codec.parse(data):
            for frame in self.codec.decode(packet):
                image = frame.to_ndarray(format="rgb24")
                with contextlib.suppress(queue.Full):
                    self.queue.put_nowait(image)

    def loop(self, hu):
        pygame = self.pygame
        keys = {
            pygame.K_HOME: 3,
            pygame.K_ESCAPE: 4,
            pygame.K_UP: 19,
            pygame.K_DOWN: 20,
            pygame.K_LEFT: 21,
            pygame.K_RIGHT: 22,
            pygame.K_RETURN: 23,
        }
        pressed = False
        shots = 0
        snapshot_at = None
        while hu.running:
            if self.snapshot and snapshot_at is None and hu.frames > 0:
                snapshot_at = time.monotonic() + self.snapshot_after
            if snapshot_at is not None and time.monotonic() >= snapshot_at:
                pygame.image.save(self.screen, self.snapshot)
                LOG.info("Saved %s", self.snapshot)
                snapshot_at = None
                self.snapshot = None
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    hu.shutdown()
                elif event.type == pygame.KEYDOWN and event.key == pygame.K_s:
                    shots += 1
                    pygame.image.save(self.screen, f"aa_headunit_{shots}.png")
                    LOG.info("Saved aa_headunit_%d.png", shots)
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    pressed = True
                    hu.send_touch(event.pos[0], event.pos[1], TOUCH_PRESS)
                elif event.type == pygame.MOUSEMOTION and pressed:
                    hu.send_touch(event.pos[0], event.pos[1], TOUCH_DRAG)
                elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
                    pressed = False
                    hu.send_touch(event.pos[0], event.pos[1], TOUCH_RELEASE)
                elif event.type in (pygame.KEYDOWN, pygame.KEYUP):
                    if event.key == pygame.K_n and event.type == pygame.KEYDOWN:
                        hu.send_night(True)
                    elif event.key == pygame.K_d and event.type == pygame.KEYDOWN:
                        hu.send_night(False)
                    elif event.key in keys:
                        hu.send_key(keys[event.key], event.type == pygame.KEYDOWN)
            try:
                image = self.queue.get(timeout=0.05)
            except queue.Empty:
                continue
            surface = pygame.image.frombuffer(image.tobytes(), image.shape[1::-1], "RGB")
            self.screen.blit(surface, (0, 0))
            pygame.display.flip()
        pygame.quit()


class HeadlessSink:
    """Optionally dumps the stream, no decoding."""

    def __init__(self, dump):
        self.dump = dump

    def sink(self, data):
        if self.dump:
            self.dump.write(data)


def parse_vid_pid(text):
    vid, pid = text.split(":")
    return int(vid, 16), int(pid, 16)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0], allow_abbrev=False)
    where = parser.add_mutually_exclusive_group(required=True)
    where.add_argument("--tcp", metavar="HOST:PORT", help="connect to the sample over TCP")
    where.add_argument(
        "--usb",
        nargs="?",
        const="",
        metavar="VID:PID",
        help="use a device in accessory mode (default: first 18d1:2d00/2d01)",
    )
    where.add_argument(
        "--aoa", metavar="VID:PID", help="switch the given device to accessory mode first"
    )
    parser.add_argument("--resolution", default="800x480", choices=sorted(RESOLUTIONS))
    parser.add_argument("--fps", type=int, default=30, choices=sorted(FPS_VALUES))
    parser.add_argument("--max-unacked", type=int, default=1)
    parser.add_argument("--dump", metavar="FILE", help="write the raw H.264 stream to FILE")
    parser.add_argument(
        "--snapshot", metavar="PNG", help="save the window a few seconds after the first frame"
    )
    parser.add_argument(
        "--snapshot-after", type=float, default=5.0, help="seconds to wait before --snapshot"
    )
    parser.add_argument("--no-display", action="store_true", help="do not decode or show video")
    parser.add_argument("--cert", metavar="PEM", help="pin the phone certificate")
    parser.add_argument("--sensors", action="store_true", help="offer a sensor channel")
    parser.add_argument("--ping-interval", type=float, default=5.0)
    parser.add_argument("--duration", type=float, default=0, help="stop after N seconds")
    parser.add_argument("--log-level", default="INFO")
    return parser


def open_transport(args):
    if args.tcp:
        host, port = args.tcp.rsplit(":", 1)
        return TcpTransport(host, int(port))
    if args.aoa:
        return UsbTransport(aoa=parse_vid_pid(args.aoa))
    return UsbTransport(vid_pid=parse_vid_pid(args.usb) if args.usb else None)


def run(args, dump):
    try:
        transport = open_transport(args)
    except (OSError, ProtocolError) as exc:
        LOG.error("Cannot open the transport: %s", exc)
        return 1

    if args.no_display:
        output = HeadlessSink(dump)
    else:
        output = VideoWindow(args.resolution, dump, args.snapshot, args.snapshot_after)
        output.start()

    hu = HeadUnit(transport, args, output.sink)
    status = 0

    def protocol_thread():
        nonlocal status
        try:
            hu.run()
        except (ProtocolError, OSError, ssl.SSLError) as exc:
            LOG.error("Session ended: %s", exc)
            status = 1
        finally:
            hu.running = False

    worker = threading.Thread(target=protocol_thread, daemon=True)
    worker.start()
    try:
        if args.no_display:
            deadline = time.monotonic() + args.duration if args.duration else None
            while hu.running and (deadline is None or time.monotonic() < deadline):
                time.sleep(0.2)
            hu.shutdown()
        else:
            output.loop(hu)
    except KeyboardInterrupt:
        hu.shutdown()
    worker.join(timeout=3.0)
    transport.close()
    LOG.info("Received %d video frames, %d bytes", hu.frames, hu.bytes)
    return status


def main():
    args = build_parser().parse_args()
    logging.basicConfig(
        level=args.log_level.upper(), format="%(asctime)s %(levelname)s %(message)s"
    )
    if args.dump:
        with open(args.dump, "wb") as dump:
            return run(args, dump)
    return run(args, None)


if __name__ == "__main__":
    sys.exit(main())
