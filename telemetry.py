"""Decoder for Lizard's telemetry frames (see "Telemetry frames" in docs/module_reference.md).

A frame line is "~<base64>" with the body frame ID, sequence number, millis, payload and CRC-16;
layout lines "__LAYOUT__v1 <frame>.<index>/<count> <name>:<type>" name and type its fields.
"""
import base64
import binascii
import re
import struct
from dataclasses import dataclass
from typing import Dict, Hashable, List, Optional, Tuple, Union

VERSION = 'v1'
HEADER = struct.Struct('<BBI')  # frame ID, sequence number, millis
CRC_SIZE = 2
LAYOUT_LINE = re.compile(r'__LAYOUT__(v\d+)(.*)')
LAYOUT_FIELD = re.compile(r' (\d+)\.(\d+)/(\d+) (\S+):([fi?])')
BUS_LINE = re.compile(r'bus\[(\d+)\]: (.*)')  # a line that a serial bus coordinator passes on from peer N

Value = Union[float, int, bool]
Field = Tuple[str, str]  # name and type


def checksum(text: str) -> int:
    """Lizard's line checksum: the XOR of all bytes of the line."""
    result = 0
    for byte in text.encode():
        result ^= byte
    return result


def split_checksum(line: str) -> Tuple[str, Optional[int]]:
    """Split a line into its text and its "@xx" checksum, which is None if the line has none."""
    if line[-3:-2] == '@':
        try:
            return line[:-3], int(line[-2:], 16)
        except ValueError:
            pass
    return line, None


def format_value(value: Value) -> str:
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, float):
        return '%.7g' % value  # the significant digits of a float32, without noise
    return str(value)


@dataclass
class Text:
    sender: Hashable
    line: str


@dataclass
class Layout:
    sender: Hashable
    frame_id: int
    index: int
    count: int
    name: str
    type: str


@dataclass
class Frame:
    sender: Hashable
    frame_id: int
    seq: int
    millis: int
    status: str  # ok, no_layout, layout_incomplete, layout_mismatch or unknown_version
    values: Optional[Dict[str, Value]]  # in field order, None unless the status is ok
    payload: bytes

    def __str__(self) -> str:
        head = f'frame {self.frame_id} seq={self.seq} millis={self.millis}'
        if self.values is None:
            return f'[{head} {self.status}] {self.payload.hex()}'
        return f'[{head}] ' + ' '.join(f'{name}={format_value(value)}' for name, value in self.values.items())


class Decoder:
    """Turns console lines into Text, Layout and Frame results and keeps the layouts of each sender.

    The sender is the device on the console, or serial bus peer N for a line starting with "bus[N]: ".
    """

    def __init__(self) -> None:
        self._versions: Dict[Hashable, str] = {}  # format version of the sender's last layout line
        self._layouts: Dict[Tuple[Hashable, int], List[Field]] = {}  # complete layouts that decode frames
        self._incoming: Dict[Tuple[Hashable, int], List[Optional[Field]]] = {}  # layouts whose lines still arrive
        self._seqs: Dict[Tuple[Hashable, int], int] = {}

    def feed(self, line: str, sender: Hashable = None) -> Union[Text, Layout, Frame]:
        """Classify a line given without line end and checksum.

        A line "bus[N]: ..." comes from peer N, so its sender is N or, if a sender is given, (sender, N).
        """
        relayed = BUS_LINE.fullmatch(line)
        if relayed:
            peer = int(relayed.group(1))
            result = self._telemetry(relayed.group(2), peer if sender is None else (sender, peer))
        else:
            result = self._telemetry(line, sender)
        return result or Text(sender, line)

    def _telemetry(self, line: str, sender: Hashable) -> Union[Layout, Frame, None]:
        if line.startswith('~'):
            body = _frame_body(line[1:])
            return None if body is None else self._frame(sender, body)
        match = LAYOUT_LINE.fullmatch(line)
        return None if match is None else self._layout(sender, match.group(1), match.group(2))

    def _layout(self, sender: Hashable, version: str, rest: str) -> Optional[Layout]:
        match = LAYOUT_FIELD.fullmatch(rest)
        if version == VERSION and (match is None or int(match.group(2)) >= int(match.group(3))):
            return None
        if self._versions.get(sender) != version:
            self._versions[sender] = version
            for table in (self._layouts, self._incoming):
                for key in [key for key in table if key[0] == sender]:
                    del table[key]
        if version != VERSION:
            return None
        frame_id, index, count = int(match.group(1)), int(match.group(2)), int(match.group(3))
        field = (match.group(4), match.group(5))
        key = (sender, frame_id)
        incoming = self._incoming.get(key)
        if index == 0 or incoming is None or len(incoming) != count:  # the layout lines of the frame start again
            incoming = self._incoming[key] = [None] * count
        incoming[index] = field
        layout = self._layouts.get(key)
        if layout is not None and (len(layout) != count or layout[index] != field):
            del self._layouts[key]  # the frame got new fields, which must never be decoded with the old ones
        if None not in incoming:
            self._layouts[key] = self._incoming.pop(key)
        return Layout(sender, frame_id, index, count, *field)

    def _frame(self, sender: Hashable, body: bytes) -> Frame:
        frame_id, seq, millis = HEADER.unpack_from(body)
        payload = body[HEADER.size:-CRC_SIZE]
        if self._versions.get(sender, VERSION) != VERSION:
            return Frame(sender, frame_id, seq, millis, 'unknown_version', None, payload)
        key = (sender, frame_id)
        last_seq = self._seqs.get(key)
        self._seqs[key] = seq
        if key in self._incoming and (last_seq is None or seq != (last_seq + 1) % 256):
            # a new definition starts its sequence anew, and a lost line may hide that its fields changed
            self._layouts.pop(key, None)
        layout = self._layouts.get(key)
        if layout is None:
            status = 'layout_incomplete' if key in self._incoming else 'no_layout'
            return Frame(sender, frame_id, seq, millis, status, None, payload)
        values = _decode(layout, payload)
        return Frame(sender, frame_id, seq, millis, 'layout_mismatch' if values is None else 'ok', values, payload)


def _frame_body(text: str) -> Optional[bytes]:
    try:
        body = base64.b64decode(text, validate=True)
    except ValueError:  # includes binascii.Error and non-ASCII text
        return None
    if len(body) < HEADER.size + CRC_SIZE:
        return None
    if binascii.crc_hqx(body[:-CRC_SIZE], 0xffff) != int.from_bytes(body[-CRC_SIZE:], 'little'):  # CCITT-FALSE
        return None
    return body


def _decode(layout: List[Field], payload: bytes) -> Optional[Dict[str, Value]]:
    numbers = sum(type_ != '?' for _, type_ in layout)
    bits_offset = 4 * numbers  # the bools follow the floats and ints as bits
    if len(payload) != bits_offset + (len(layout) - numbers + 7) // 8:
        return None
    values: Dict[str, Value] = {}
    offset = bit = 0
    for name, type_ in layout:
        if type_ == '?':
            values[name] = bool(payload[bits_offset + bit // 8] >> bit % 8 & 1)
            bit += 1
        else:
            values[name] = struct.unpack_from('<' + type_, payload, offset)[0]
            offset += 4
    return values
