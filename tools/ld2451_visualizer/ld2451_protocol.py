"""LD2451 wire protocol helpers.

The radar carries the same frames over its 115200-baud UART and BLE bridge.
All multibyte fields are little endian.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import struct


REPORT_HEADER = b"\xF4\xF3\xF2\xF1"
REPORT_TAIL = b"\xF8\xF7\xF6\xF5"
COMMAND_HEADER = b"\xFD\xFC\xFB\xFA"
COMMAND_TAIL = b"\x04\x03\x02\x01"


@dataclass(frozen=True)
class Target:
    angle_deg: int
    distance_m: int
    approaching: bool
    speed_kmh: int
    snr: int

    @property
    def x_m(self) -> float:
        return self.distance_m * math.sin(math.radians(self.angle_deg))

    @property
    def y_m(self) -> float:
        return self.distance_m * math.cos(math.radians(self.angle_deg))


@dataclass(frozen=True)
class Report:
    alarm: bool
    targets: tuple[Target, ...]


@dataclass(frozen=True)
class Ack:
    command: int
    status: int
    payload: bytes


def build_command(command: int, value: bytes = b"") -> bytes:
    body = struct.pack("<H", command) + value
    return COMMAND_HEADER + struct.pack("<H", len(body)) + body + COMMAND_TAIL


def parse_report(frame: bytes) -> Report:
    if len(frame) < 10 or not frame.startswith(REPORT_HEADER) or not frame.endswith(REPORT_TAIL):
        raise ValueError("not an LD2451 report frame")
    data_length = int.from_bytes(frame[4:6], "little")
    body = frame[6:-4]
    if len(body) != data_length:
        raise ValueError("invalid report length")
    # LD2451 sends a zero-length report as its idle/no-target heartbeat.
    if not body:
        return Report(False, ())
    if len(body) < 2:
        raise ValueError("invalid report length")
    count, alarm = body[0], body[1]
    if len(body) != 2 + count * 5:
        raise ValueError("target count does not match report length")
    targets = []
    for offset in range(2, len(body), 5):
        angle_raw, distance, direction, speed, snr = body[offset : offset + 5]
        targets.append(Target(angle_raw - 0x80, distance, direction == 0, speed, snr))
    return Report(bool(alarm), tuple(targets))


def parse_ack(frame: bytes) -> Ack:
    if len(frame) < 12 or not frame.startswith(COMMAND_HEADER) or not frame.endswith(COMMAND_TAIL):
        raise ValueError("not an LD2451 ACK frame")
    data_length = int.from_bytes(frame[4:6], "little")
    body = frame[6:-4]
    if len(body) != data_length or len(body) < 4:
        raise ValueError("invalid ACK length")
    response_command, status = struct.unpack_from("<HH", body)
    return Ack(response_command & 0x00FF, status, body[4:])


class FrameStream:
    """Reassembles report and ACK frames split across BLE notifications."""

    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, chunk: bytes) -> list[bytes]:
        self.buffer.extend(chunk)
        frames: list[bytes] = []
        while True:
            report_at = self.buffer.find(REPORT_HEADER)
            command_at = self.buffer.find(COMMAND_HEADER)
            positions = [p for p in (report_at, command_at) if p >= 0]
            if not positions:
                if len(self.buffer) > 3:
                    del self.buffer[:-3]
                break
            start = min(positions)
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 6:
                break
            length = int.from_bytes(self.buffer[4:6], "little")
            total = 4 + 2 + length + 4
            if length > 1024:
                del self.buffer[0]
                continue
            if len(self.buffer) < total:
                break
            candidate = bytes(self.buffer[:total])
            expected_tail = REPORT_TAIL if candidate.startswith(REPORT_HEADER) else COMMAND_TAIL
            if not candidate.endswith(expected_tail):
                del self.buffer[0]
                continue
            frames.append(candidate)
            del self.buffer[:total]
        return frames
