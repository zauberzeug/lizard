import base64
import binascii
import logging
import random
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from telemetry import Decoder, Layout, Text, check_line, encode_frame, line_checksum, main  # noqa: E402

PLEXUS_FRAME = '~AQdA4gEAtvOdPwAAEkIBQFM='
PLEXUS_LAYOUT = ['__LAYOUT__v1 1.0 motor.position:f', '__LAYOUT__v1 1.1 imu.temp:f', '__LAYOUT__v1 1.2 motor.enabled:?']
CORE_FRAME = '~ASpO4gEAmpmZPpqZmT62850/AAASQjLS'
CORE_NAMES = ['wheels.l_speed', 'wheels.r_speed', 'arm2.motor.position', 'arm2.imu.temp']


def with_checksum(line: str) -> str:
    return f'{line}@{line_checksum(line)}\n'


def feed(decoder: Decoder, lines: list, sender=None) -> list:
    """Send lines through the console path: add the checksum, check it, feed the content."""
    events = []
    for line in lines:
        text, intact = check_line(with_checksum(line))
        assert intact
        events.append(decoder.feed(text, sender))
    return events


def layout_lines(frame: int, fields: list) -> list:
    return [f'__LAYOUT__v1 {frame}.{index} {name}:{type_}' for index, (name, type_, _) in enumerate(fields)]


def encode(frame: int, seq: int, millis: int, fields: list) -> str:
    return encode_frame(frame, seq, millis, [(type_, value) for _, type_, value in fields])


def f32(value: float) -> float:
    return struct.unpack('<f', struct.pack('<f', value))[0]


def f16(value: float) -> float:
    return struct.unpack('<e', struct.pack('<e', value))[0]


def reencode(body: bytes) -> str:
    return '~' + base64.b64encode(body).decode()


def with_crc(data: bytes) -> bytes:
    return data + binascii.crc_hqx(data, 0xffff).to_bytes(2, 'little')


def test_plexus_example():
    decoder = Decoder()
    layouts = feed(decoder, PLEXUS_LAYOUT, sender=2)
    assert [(e.version, e.frame, e.index, e.name, e.type) for e in layouts] == [
        ('v1', 1, 0, 'motor.position', 'f'), ('v1', 1, 1, 'imu.temp', 'f'), ('v1', 1, 2, 'motor.enabled', '?')]
    [frame] = feed(decoder, [PLEXUS_FRAME], sender=2)
    assert (frame.sender, frame.id, frame.seq, frame.millis, frame.status) == (2, 1, 7, 123456, 'ok')
    assert frame.values == {'motor.position': f32(1.234), 'imu.temp': 36.5, 'motor.enabled': True}
    assert str(frame) == '[frame 1 from 2 seq=7 millis=123456] motor.position=1.234 imu.temp=36.5 motor.enabled=true'
    assert encode_frame(1, 7, 123456, [('f', 1.234), ('f', 36.5), ('?', True)]) == PLEXUS_FRAME
    assert decoder.feed(PLEXUS_FRAME).status == 'no_layout'  # layouts are per sender


def test_core_example():
    decoder = Decoder()
    feed(decoder, [f'__LAYOUT__v1 1.{index} {name}:f' for index, name in enumerate(CORE_NAMES)])
    [frame] = feed(decoder, [CORE_FRAME])
    assert (frame.sender, frame.id, frame.seq, frame.millis, frame.status) == (None, 1, 42, 123470, 'ok')
    assert frame.values == dict(zip(CORE_NAMES, map(f32, [0.3, 0.3, 1.234, 36.5])))
    assert str(frame) == ('[frame 1 seq=42 millis=123470] '
                          'wheels.l_speed=0.3 wheels.r_speed=0.3 arm2.motor.position=1.234 arm2.imu.temp=36.5')
    assert encode_frame(1, 42, 123470, [('f', value) for value in (0.3, 0.3, 1.234, 36.5)]) == CORE_FRAME


def test_frame_before_its_layout_shows_the_payload_as_hex():
    frame = Decoder().feed(CORE_FRAME)
    assert (frame.status, frame.values) == ('no_layout', None)
    assert str(frame) == '[frame 1 seq=42 millis=123470 no_layout] 9a99993e9a99993eb6f39d3f00001242'


def test_check_line():
    assert line_checksum(PLEXUS_FRAME) == '50'
    assert line_checksum('ä') == '67'  # over the UTF-8 bytes c3 a4
    assert check_line(PLEXUS_FRAME + '@50\r\n') == (PLEXUS_FRAME, True)
    assert check_line((PLEXUS_FRAME + '@50\n').encode()) == (PLEXUS_FRAME, True)
    assert check_line(PLEXUS_FRAME + '@51') == (PLEXUS_FRAME, False)
    assert check_line('I (312) boot: ESP-IDF v5.3.1\n') == ('I (312) boot: ESP-IDF v5.3.1', None)
    assert check_line('mail me@zz') == ('mail me@zz', None)  # no hex after '@' is no checksum, as in monitor.py


def test_round_trip_of_mixed_fields():
    fields = [
        ('speed', 'f', 0.1), ('b0', '?', True), ('count', 'i', -123456), ('b1', '?', False), ('b2', '?', True),
        ('half', 'e', 0.3333), ('b3', '?', False), ('b4', '?', False), ('b5', '?', True), ('min', 'i', -2**31),
        ('b6', '?', True), ('b7', '?', False), ('b8', '?', True), ('b9', '?', True), ('big', 'f', -1e30),
        ('b10', '?', False),
    ]
    decoder = Decoder()
    feed(decoder, layout_lines(3, fields))
    [frame] = feed(decoder, [encode(3, 200, 4000000000, fields)])
    # numbers in index order, then the 11 bools LSB first: 0b01100101 for b0..b7, 0b011 for b8..b10
    assert frame.payload == struct.pack('<fieif', 0.1, -123456, 0.3333, -2**31, -1e30) + bytes([0x65, 0x03])
    assert frame.status == 'ok'
    expected = {name: value for name, _, value in fields}
    expected.update(speed=f32(0.1), half=f16(0.3333), big=f32(-1e30))
    assert frame.values == expected
    assert list(frame.values) == [name for name, _, _ in fields]
    assert str(frame).startswith('[frame 3 seq=200 millis=4000000000] speed=0.1 b0=true count=-123456 b1=false ')
    assert ' half=0.3333 ' in str(frame) and ' min=-2147483648 ' in str(frame) and ' big=-1e+30 ' in str(frame)


def test_bools_follow_the_numbers_lsb_first():
    line = encode_frame(1, 0, 0, [('?', True), ('i', -1)] + [('?', False)] * 7 + [('?', True)])
    assert base64.b64decode(line[1:])[6:-2].hex() == 'ffffffff0101'


@pytest.mark.parametrize('line', [
    '~hallo',  # not base64
    '~hallohallo12',  # nine bytes of base64, but the CRC does not match
    '~AQdA4gEAtvOd!PwAAEkIBQFM',  # bad base64 character
    '~AQdA4gEAtvOdPwAAEkIBQF',  # length not a multiple of 4
    '~AQdA4gEAtvOdPwAAEkIBQFM=\n',  # line end not stripped
    '~AAAAAAA=',  # five bytes, shorter than header and CRC
    reencode(with_crc(bytes([1, 7, 64, 226, 1]))),  # valid CRC, but only seven bytes
    '~',
    '~~AQdA4gEAtvOdPwAAEkIBQFM=',
    reencode(base64.b64decode(PLEXUS_FRAME[1:])[:-1] + b'\x54'),  # bad CRC
    reencode(bytes(b ^ (0x01 if i == 14 else 0) for i, b in enumerate(base64.b64decode(PLEXUS_FRAME[1:])))),  # bit
])
def test_lines_that_are_not_frames_stay_text(line):
    decoder = Decoder()
    feed(decoder, PLEXUS_LAYOUT)
    event = decoder.feed(line)
    assert event == Text(None, line)
    assert str(event) == line
    assert not decoder.stats


def test_crc_catches_damage_the_line_checksum_misses():
    # flipping bit 0 of two characters keeps both the XOR checksum and the base64 alphabet intact
    damaged = PLEXUS_FRAME[:10] + 'w' + PLEXUS_FRAME[11:13] + 'Q' + PLEXUS_FRAME[14:]
    assert (PLEXUS_FRAME[10], PLEXUS_FRAME[13]) == ('v', 'P')
    assert check_line(damaged + '@50') == (damaged, True)
    assert Decoder().feed(damaged) == Text(None, damaged)


def test_smallest_frame_has_an_empty_payload():
    line = encode_frame(9, 1, 2, [])
    assert len(base64.b64decode(line[1:])) == 8
    frame = Decoder().feed(line)
    assert (frame.id, frame.status, frame.payload) == (9, 'no_layout', b'')
    assert str(frame) == '[frame 9 seq=1 millis=2 no_layout]'


@pytest.mark.parametrize('line', [
    '__LAYOUT__v1 1.0 name:x',
    '__LAYOUT__v1 1.0 name',
    '__LAYOUT__v1 1.0 a:b:f',
    '__LAYOUT__v1 1.0 a b:f',
    '__LAYOUT__v1 1.0 name:f ',
    '__LAYOUT__v1 256.0 name:f',
    '__LAYOUT__v1 1 name:f',
    '__LAYOUT__v1',
    '__LAYOUT__ 1.0 name:f',
    '__LAYOUT__vx 1.0 name:f',
    '__LAYOUT__v1x 1.0 name:f',
])
def test_malformed_layout_lines_stay_text(line):
    decoder = Decoder()
    assert decoder.feed(line) == Text(None, line)
    assert not decoder.layouts and not decoder.versions


def test_unknown_version_warns_once_and_ignores_the_senders_frames(caplog):
    decoder = Decoder()
    feed(decoder, PLEXUS_LAYOUT, sender=3)
    with caplog.at_level(logging.WARNING):
        first, second = feed(decoder, ['__LAYOUT__v2 1.0 motor.position:f', '__LAYOUT__v2 1.1 whatever'], sender=2)
        frames = feed(decoder, [PLEXUS_FRAME, PLEXUS_FRAME], sender=2)
    assert first == Layout(2, 'v2', None, None, None, None, '__LAYOUT__v2 1.0 motor.position:f')
    assert str(second) == '__LAYOUT__v2 1.1 whatever'
    assert [(frame.status, frame.values) for frame in frames] == [('unknown_version', None)] * 2
    assert str(frames[0]) == '[frame 1 from 2 seq=7 millis=123456 unknown_version] b6f39d3f0000124201'
    assert [record.getMessage() for record in caplog.records] == [
        'unknown telemetry format version v2 from 2, ignoring its frames']
    assert decoder.stats[(2, 1)].status == {'unknown_version': 2}
    assert feed(decoder, [PLEXUS_FRAME], sender=3)[0].status == 'ok'  # other senders are unaffected


def test_version_change_drops_the_layouts_of_the_old_version():
    decoder = Decoder()
    fields = [('a', 'f', 1.0), ('b', 'f', 2.0)]
    feed(decoder, layout_lines(1, fields))
    assert feed(decoder, [encode(1, 0, 0, fields)])[0].status == 'ok'
    feed(decoder, ['__LAYOUT__v2 1.0 a:f'])
    assert feed(decoder, [encode(1, 1, 10, fields)])[0].status == 'unknown_version'
    feed(decoder, layout_lines(1, fields)[:1])
    assert feed(decoder, [encode(1, 2, 20, fields)])[0].status == 'layout_mismatch'  # b is gone


def test_layout_mismatch():
    decoder = Decoder()
    feed(decoder, ['__LAYOUT__v1 1.0 a:f', '__LAYOUT__v1 1.1 b:f'])
    [frame] = feed(decoder, [encode_frame(1, 0, 0, [('f', 1.0)])])
    assert (frame.status, frame.values) == ('layout_mismatch', None)
    assert str(frame) == '[frame 1 seq=0 millis=0 layout_mismatch] 0000803f'


def test_layout_line_replaces_the_same_index_and_reset_forgets_the_sender():
    decoder = Decoder()
    feed(decoder, ['__LAYOUT__v1 1.0 old:f', '__LAYOUT__v1 1.0 new:i'])
    assert feed(decoder, [encode_frame(1, 0, 0, [('i', 7)])])[0].values == {'new': 7}
    decoder.reset()
    assert not decoder.layouts and not decoder.stats and not decoder.versions
    assert feed(decoder, [encode_frame(1, 1, 10, [('i', 7)])])[0].status == 'no_layout'


def test_seq_gaps_count_missing_numbers_across_the_wrap():
    decoder = Decoder()
    for i, seq in enumerate([253, 254, 255, 0, 1]):
        decoder.feed(encode_frame(1, seq, 1000 + 10 * i, []))
    assert decoder.stats[(None, 1)].seq_gaps == 0
    for i, seq in enumerate([250, 252, 254, 1, 2, 5]):
        decoder.feed(encode_frame(2, seq, 1000 + 10 * i, []))
    stats = decoder.stats[(None, 2)]
    assert stats.seq_gaps == 1 + 1 + 2 + 2  # 251, 253, 255 and 0, 3 and 4
    assert (stats.frames, stats.millis_resets, stats.status) == (6, 0, {'no_layout': 6})
    decoder.feed(encode_frame(2, 5, 1050, []))  # the same line again
    decoder.feed(encode_frame(2, 5, 1060, []))  # the same seq later: 255 frames missing, then this one
    assert (stats.frames, stats.seq_gaps) == (8, 6 + 255)


def test_millis_reset_counts_a_reboot_and_does_not_count_its_seq_restart_as_gap():
    decoder = Decoder()
    for seq, millis in [(10, 50000), (11, 50100), (12, 49100), (0, 300), (1, 400)]:
        decoder.feed(encode_frame(1, seq, millis, []))
    stats = decoder.stats[(None, 1)]
    assert stats.millis_resets == 1  # 49100 is only 1000 ms back, 300 is a reboot
    assert stats.seq_gaps == 0
    assert (stats.last_seq, stats.last_millis) == (1, 400)


LOSS_FIELDS = [('position', 'f', 1.5), ('a', '?', False), ('b', '?', True), ('c', '?', False)]


def test_lost_layout_line_of_a_middle_bool():
    """Non-strict decoding shifts the following bits onto the wrong names, strict mode refuses the frame."""
    lines = layout_lines(1, LOSS_FIELDS)
    del lines[2]
    line = encode(1, 0, 0, LOSS_FIELDS)

    lenient = Decoder(strict=False)
    feed(lenient, lines)
    [frame] = feed(lenient, [line])
    assert frame.status == 'ok'
    assert frame.values == {'position': 1.5, 'a': False, 'c': True}  # c shows the bit of b

    strict = Decoder()
    feed(strict, lines)
    [frame] = feed(strict, [line])
    assert (frame.status, frame.values) == ('layout_incomplete', None)


def test_lost_layout_line_of_the_last_field_is_not_detectable():
    """Finding: a lost last field that is a bool sharing its byte leaves the payload length unchanged, so neither mode
    notices and the field is silently missing. Only a field count in the layout line would make this detectable."""
    lines = layout_lines(1, LOSS_FIELDS)[:-1]
    line = encode(1, 0, 0, LOSS_FIELDS)
    for strict in (True, False):
        decoder = Decoder(strict=strict)
        feed(decoder, lines)
        [frame] = feed(decoder, [line])
        assert frame.status == 'ok'
        assert frame.values == {'position': 1.5, 'a': False, 'b': True}


def test_lost_layout_line_of_the_last_field_is_detected_if_it_changes_the_payload_length():
    number = [('a', '?', True), ('x', 'f', 2.0)]
    ninth_bool = [(f'b{i}', '?', True) for i in range(9)]
    for fields in (number, ninth_bool):
        decoder = Decoder()
        feed(decoder, layout_lines(1, fields)[:-1])
        assert feed(decoder, [encode(1, 0, 0, fields)])[0].status == 'layout_mismatch'


def counted_layout_lines(frame, fields):
    return [f'__LAYOUT__v1 {frame}.{index}/{len(fields)} {name}:{type_}' for index, (name, type_, _) in enumerate(fields)]


def test_field_count_detects_a_lost_last_layout_line():
    """With the field count in the layout lines the finding above is gone: the frame stays incomplete."""
    lines = counted_layout_lines(1, LOSS_FIELDS)
    line = encode(1, 0, 0, LOSS_FIELDS)
    for strict in (True, False):
        decoder = Decoder(strict=strict)
        feed(decoder, lines[:-1])
        assert feed(decoder, [line])[0].status == 'layout_incomplete'
        feed(decoder, lines[-1:])
        [frame] = feed(decoder, [line])
        assert (frame.status, frame.values) == ('ok', {'position': 1.5, 'a': False, 'b': True, 'c': False})


def test_counted_and_uncounted_layout_lines_are_both_read():
    decoder = Decoder()
    [counted, uncounted] = feed(decoder, ['__LAYOUT__v1 1.0/2 a:f', '__LAYOUT__v1 2.0 b:i'])
    assert (counted.frame, counted.index, counted.count, counted.name) == (1, 0, 2, 'a')
    assert (uncounted.frame, uncounted.index, uncounted.count, uncounted.name) == (2, 0, None, 'b')


def test_a_new_field_count_redefines_the_frame():
    decoder = Decoder()
    feed(decoder, counted_layout_lines(1, [('a', 'f', 1.0), ('b', 'f', 2.0), ('c', 'f', 3.0)]))
    feed(decoder, counted_layout_lines(1, [('x', 'i', 5), ('y', 'i', 6)]))
    [frame] = feed(decoder, [encode(1, 0, 0, [('x', 'i', 5), ('y', 'i', 6)])])
    assert (frame.status, frame.values) == ('ok', {'x': 5, 'y': 6})


def test_a_redefined_frame_counts_its_seq_from_anew():
    decoder = Decoder()
    feed(decoder, counted_layout_lines(1, [('a', 'f', 1.0)]))
    feed(decoder, [encode(1, seq, 1000 + 10 * seq, [('a', 'f', 1.0)]) for seq in range(10)])
    feed(decoder, counted_layout_lines(1, [('b', 'f', 2.0), ('c', 'f', 3.0)]))  # core.clear_telemetry(), core.telemetry(b, c)
    feed(decoder, [encode(1, seq, 1200 + 10 * seq, [('b', 'f', 2.0), ('c', 'f', 3.0)]) for seq in range(5)])
    feed(decoder, counted_layout_lines(1, [('x', 'i', 5), ('y', 'f', 6.0)]))  # same count, other fields
    feed(decoder, [encode(1, seq, 1300 + 10 * seq, [('x', 'i', 5), ('y', 'f', 6.0)]) for seq in range(3)])
    feed(decoder, [encode(1, 5, 1400, [('x', 'i', 5), ('y', 'f', 6.0)])])  # 3 and 4 are missing
    assert decoder.stats[(None, 1)].seq_gaps == 2
    feed(decoder, counted_layout_lines(1, [('x', 'i', 5), ('y', 'f', 6.0)]))  # core.clear_telemetry(), the same core.telemetry
    feed(decoder, [encode(1, 0, 1500, [('x', 'i', 5), ('y', 'f', 6.0)])])  # seq starts at 0 again
    assert decoder.stats[(None, 1)].seq_gaps == 2
    feed(decoder, counted_layout_lines(1, [('x', 'i', 5), ('y', 'f', 6.0)]))  # core.telemetry_info(): the same layout
    feed(decoder, [encode(1, seq, 1600 + 10 * seq, [('x', 'i', 5), ('y', 'f', 6.0)]) for seq in (1, 5)])  # 2, 3, 4 lost
    assert decoder.stats[(None, 1)].seq_gaps == 2 + 3  # a repeated layout hides no gap
    feed(decoder, counted_layout_lines(1, [('x', 'i', 5), ('y', 'f', 6.0)]))
    feed(decoder, [encode(1, 0, 100, [('x', 'i', 5), ('y', 'f', 6.0)])])  # a reboot with the same startup
    assert decoder.stats[(None, 1)].millis_resets == 1


@pytest.mark.parametrize('line', ['__LAYOUT__v1 1.0/0 a:f', '__LAYOUT__v1 1.2/2 a:f', '__LAYOUT__v1 1.0/ a:f',
                                  '__LAYOUT__v1 1.0/257 a:f', '__LAYOUT__v1 1.0/2/3 a:f'])
def test_malformed_field_counts_stay_text(line):
    assert isinstance(Decoder().feed(line), Text)


def test_garbage_never_raises():
    rng = random.Random(0)
    decoder = Decoder(strict=False)
    for sender in (None, 2):
        for _ in range(200):
            type_ = rng.choice('fie?')
            decoder.feed(f'__LAYOUT__v1 {rng.randrange(1, 4)}.{rng.randrange(6)} n{rng.randrange(9)}:{type_}', sender)
    alphabet = 'AQdg4E+/=~_:.?fie 019v@LAYOUT\x00ä'
    for _ in range(20000):
        body = with_crc(bytes([rng.randrange(1, 4)]) + bytes(rng.randrange(256) for _ in range(rng.randrange(30))))
        prefix = rng.choice(['~', '__LAYOUT__v1 ', '__LAYOUT__v', ''])
        noise = prefix + ''.join(rng.choice(alphabet) for _ in range(rng.randrange(40)))
        for line in (reencode(body), noise):
            str(decoder.feed(line, rng.choice((None, 2))))


def test_cli_prints_text_unchanged_and_decodes_frames(tmp_path, capsys):
    early = encode_frame(1, 0, 100, [('f', 0.5), ('?', True)])
    frame = encode_frame(1, 1, 110, [('f', 0.25), ('?', False)])
    lines = [
        'I (312) boot: ESP-IDF v5.3.1',
        with_checksum('core 0.1 0.2').strip(),
        with_checksum(early).strip(),
        with_checksum('__LAYOUT__v1 1.0 wheels.speed:f').strip(),
        with_checksum('__LAYOUT__v1 1.1 estop.active:?').strip(),
        with_checksum(frame).strip(),
        f'{frame}@{int(line_checksum(frame), 16) ^ 1:02x}',  # damaged line
        with_checksum('~hallo').strip(),
        with_checksum('__LAYOUT__v1 1.0 wheels.l_speed:f').strip(),
        with_checksum(encode_frame(1, 2, 120, [('f', -1.0), ('?', True)])).strip(),
    ]
    log = tmp_path / 'console.log'
    log.write_text('\r\n'.join(lines) + '\r\n')

    assert main([str(log), '--csv', str(tmp_path / 'csv'), '--stats']) == 0
    out, err = capsys.readouterr()
    expected = lines[:]
    expected[2] = '[frame 1 seq=0 millis=100 no_layout] 0000003f01'
    expected[5] = '[frame 1 seq=1 millis=110] wheels.speed=0.25 estop.active=false'
    expected[9] = '[frame 1 seq=2 millis=120] wheels.l_speed=-1 estop.active=true'
    assert out.splitlines() == expected
    assert err == 'frame 1: 3 frames, 0 missing seq, 0 millis resets (no_layout 1, ok 2)\n'
    assert (tmp_path / 'csv' / 'frame_1.csv').read_text().splitlines() == [
        'millis,wheels.speed,estop.active', '110,0.25,false']
    assert (tmp_path / 'csv' / 'frame_1_2.csv').read_text().splitlines() == [
        'millis,wheels.l_speed,estop.active', '120,-1,true']


def test_import_has_no_side_effects():
    result = subprocess.run([sys.executable, '-c', 'import telemetry'], cwd=ROOT, capture_output=True, text=True,
                            check=True)
    assert (result.stdout, result.stderr) == ('', '')
