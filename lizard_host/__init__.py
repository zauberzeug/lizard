"""Shared host-side code of the Lizard tools: telemetry decoding, console line checksums and serial device discovery.

Import it as ``import lizard_host as lh`` and use the names below,
e.g. ``lh.TelemetryDecoder()`` or ``lh.split_checksum(line)``.
"""
from .console_line import checksum, format_line, split_checksum
from .serial_devices import IS_JETSON, find_devices, resolve_device
from .telemetry import Frame, Layout, TelemetryDecoder, Text

__all__ = [
    'IS_JETSON',
    'Frame',
    'Layout',
    'TelemetryDecoder',
    'Text',
    'checksum',
    'find_devices',
    'format_line',
    'resolve_device',
    'split_checksum',
]
