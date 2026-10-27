import base64
import binascii
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import telemetry  # noqa: E402
from telemetry import Decoder, Frame, Layout, Text  # noqa: E402

# Recorded from an ESP32 after
#   float pos = 1.234, int state = 8, bool on = true, bool b0 = true ... bool b8 = false (true every third),
#   float neg = -1e30, int low = -2147483648
# core.telemetry(pos, state, on, 100)
LAYOUT_1 = ['__LAYOUT__v1 1.0/3 pos:f@5e', '__LAYOUT__v1 1.1/3 state:i@4b', '__LAYOUT__v1 1.2/3 on:?@68']
FRAMES_1 = ['~AQDP5Q4AtvOdPwgAAAABTJA=@22', '~AQE55g4AtvOdPwgAAAABdbU=@7c']
# core.telemetry(core.millis, neg, low, b0, b1, b2, b3, b4, b5, b6, b7, b8, 250)
LAYOUT_2 = [
    '__LAYOUT__v1 2.0/12 core.millis:i@25', '__LAYOUT__v1 2.1/12 neg:f@6c', '__LAYOUT__v1 2.2/12 low:i@78',
    '__LAYOUT__v1 2.3/12 b0:?@09', '__LAYOUT__v1 2.4/12 b1:?@0f', '__LAYOUT__v1 2.5/12 b2:?@0d',
    '__LAYOUT__v1 2.6/12 b3:?@0f', '__LAYOUT__v1 2.7/12 b4:?@09', '__LAYOUT__v1 2.8/12 b5:?@07',
    '__LAYOUT__v1 2.9/12 b6:?@05', '__LAYOUT__v1 2.10/12 b7:?@3c', '__LAYOUT__v1 2.11/12 b8:?@32',
]
FRAME_2 = '~AgBB6A4AQegOAMrySfEAAACASQCVmQ==@59'
# the last frames before core.telemetry_info(), and what follows while both frames run
BEFORE_REPEAT = ['~AgMn6w4AJ+sOAMrySfEAAACASQClpg==@1c', '~AQ5P6w4AtvOdPwgAAAABdtU=@6c', '~AQ+z6w4AtvOdPwgAAAABTHQ=@50']
REPEAT = [
    '__LAYOUT__v1 1.0/3 pos:f@5e', '__LAYOUT__v1 1.1/3 state:i@4b', '__LAYOUT__v1 1.2/3 on:?@68',
    '__LAYOUT__v1 2.0/12 core.millis:i@25',
    '~ARAZ7A4AtvOdPwgAAAAByTg=@29', '~AgQZ7A4AGewOAMrySfEAAACASQCDaQ==@4b',
    '__LAYOUT__v1 2.1/12 neg:f@6c', '__LAYOUT__v1 2.2/12 low:i@78', '__LAYOUT__v1 2.3/12 b0:?@09',
    '__LAYOUT__v1 2.4/12 b1:?@0f', '__LAYOUT__v1 2.5/12 b2:?@0d', '__LAYOUT__v1 2.6/12 b3:?@0f',
    '__LAYOUT__v1 2.7/12 b4:?@09', '__LAYOUT__v1 2.8/12 b5:?@07', '__LAYOUT__v1 2.9/12 b6:?@05',
    '__LAYOUT__v1 2.10/12 b7:?@3c', '__LAYOUT__v1 2.11/12 b8:?@32',
    '~ARF77A4AtvOdPwgAAAAB1r4=@7e',
]
# core.clear_telemetry() and core.telemetry(pos, on, state, 100)
REDEFINED = [
    '__LAYOUT__v1 1.0/3 pos:f@5e', '__LAYOUT__v1 1.1/3 on:?@6b', '__LAYOUT__v1 1.2/3 state:i@48',
    '~AQD28Q4AtvOdPwgAAAABLd8=@02',
]
# core.clear_telemetry() and core.telemetry(pos, state, 100)
SHORTENED = ['__LAYOUT__v1 1.0/2 pos:f@5f', '__LAYOUT__v1 1.1/2 state:i@4a', '~AQCE9Q4AtvOdPwgAAAADVw==@79']


def feed(decoder: Decoder, lines: list, sender=None) -> list:
    results = []
    for line in lines:
        text, check = telemetry.split_checksum(line)
        assert check == telemetry.checksum(text)
        results.append(decoder.feed(text, sender))
    return results


def strip(line: str) -> str:
    return telemetry.split_checksum(line)[0]


def with_crc(body: bytes) -> str:
    return '~' + base64.b64encode(body + binascii.crc_hqx(body, 0xffff).to_bytes(2, 'little')).decode()


def encode(frame_id: int, seq: int, millis: int, payload: bytes) -> str:
    return with_crc(struct.pack('<BBI', frame_id, seq, millis) + payload)


def with_seq(line: str, seq: int) -> str:
    body = bytearray(base64.b64decode(strip(line)[1:])[:-2])
    body[1] = seq
    return with_crc(bytes(body))


def flip_bit(line: str) -> str:
    body = bytearray(base64.b64decode(strip(line)[1:]))
    body[7] ^= 0x01
    return '~' + base64.b64encode(body).decode()


def f32(value: float) -> float:
    return struct.unpack('<f', struct.pack('<f', value))[0]


def test_recorded_frame():
    *layouts, frame = feed(Decoder(), LAYOUT_1 + FRAMES_1[:1])
    assert layouts == [Layout(None, 1, 0, 3, 'pos', 'f'), Layout(None, 1, 1, 3, 'state', 'i'),
                       Layout(None, 1, 2, 3, 'on', '?')]
    assert (frame.frame_id, frame.seq, frame.millis, frame.status) == (1, 0, 976335, 'ok')
    assert frame.values == {'pos': f32(1.234), 'state': 8, 'on': True}
    assert str(frame) == '[frame 1 seq=0 millis=976335] pos=1.234 state=8 on=true'


def test_recorded_frame_with_nine_bools():
    frame = feed(Decoder(), LAYOUT_2 + [FRAME_2])[-1]
    assert frame.status == 'ok'
    assert frame.values['core.millis'] == frame.millis
    assert frame.values['neg'] == f32(-1e30)
    assert frame.values['low'] == -2**31
    assert [frame.values[f'b{i}'] for i in range(9)] == [i % 3 == 0 for i in range(9)]
    assert str(frame).endswith(' neg=-1e+30 low=-2147483648 b0=true b1=false b2=false b3=true b4=false b5=false '
                               'b6=true b7=false b8=false')


def test_documented_example():
    lines = ['__LAYOUT__v1 1.0/3 motor.position:f@6e', '__LAYOUT__v1 1.1/3 motor.axis_state:i@52',
             '__LAYOUT__v1 1.2/3 motor.enabled:?@49', '~AQdA4gEAtvOdPwgAAAABwBc=@5c']
    assert str(feed(Decoder(), lines)[-1]) == \
        '[frame 1 seq=7 millis=123456] motor.position=1.234 motor.axis_state=8 motor.enabled=true'


def test_frames_keep_decoding_while_layout_lines_repeat():
    results = feed(Decoder(), LAYOUT_1 + LAYOUT_2 + BEFORE_REPEAT + REPEAT)
    assert [result.status for result in results if isinstance(result, Frame)] == ['ok'] * 6


def test_frames_keep_decoding_while_layout_lines_repeat_across_the_sequence_wrap():
    decoder = Decoder()
    feed(decoder, LAYOUT_1)
    decoder.feed(with_seq(FRAMES_1[0], 255))
    feed(decoder, LAYOUT_1[:1])
    assert decoder.feed(strip(FRAMES_1[0])).status == 'ok'


def test_reused_frame_id():
    results = feed(Decoder(), LAYOUT_1 + FRAMES_1 + REDEFINED + SHORTENED)
    assert list(results[-4].values) == ['pos', 'on', 'state']
    assert results[-1].values == {'pos': f32(1.234), 'state': 8}


@pytest.mark.parametrize('frames', [FRAMES_1, []])  # with and without frames of the old layout
def test_reused_frame_id_is_not_decoded_with_the_old_layout_when_layout_lines_are_lost(frames):
    decoder = Decoder()
    feed(decoder, LAYOUT_1 + frames)
    # the first line of the new layout matches the old one, the lines that differ are lost
    assert feed(decoder, REDEFINED[:1] + REDEFINED[3:])[-1].status == 'layout_incomplete'
    assert feed(decoder, REDEFINED)[-1].status == 'ok'


@pytest.mark.parametrize('lines', [
    REDEFINED[1:2] + REDEFINED[3:],  # a differing line
    SHORTENED[:1] + SHORTENED[2:],  # a differing count
])
def test_differing_layout_line_drops_the_old_layout_although_the_sequence_seems_to_continue(lines):
    decoder = Decoder()
    feed(decoder, LAYOUT_1)
    decoder.feed(with_seq(FRAMES_1[0], 255))  # the new definition starts again at 0
    assert feed(decoder, lines)[-1].status == 'layout_incomplete'


@pytest.mark.parametrize('lines', [
    LAYOUT_1[:2] + REDEFINED[:1] + REDEFINED[2:],  # a line with index 0 starts the layout again
    SHORTENED[1:2] + REDEFINED[1:],  # so does a line with another count
])
def test_lines_of_an_older_layout_are_not_mixed_into_a_new_one(lines):
    assert feed(Decoder(), lines)[-1].status == 'layout_incomplete'


def test_lost_last_layout_line():
    decoder = Decoder()
    assert feed(decoder, LAYOUT_2[:-1] + [FRAME_2])[-1].status == 'layout_incomplete'
    assert feed(decoder, LAYOUT_2[-1:] + [FRAME_2])[-1].status == 'ok'


def test_lost_last_bool_layout_line_sharing_its_byte():
    decoder = Decoder()
    decoder.feed('__LAYOUT__v1 1.0/2 a:?')  # the line of b is lost; a and b share one byte of the payload
    frame = decoder.feed(encode(1, 0, 0, bytes([0b11])))
    assert (frame.status, frame.values) == ('layout_incomplete', None)


def test_no_layout():
    frame = Decoder().feed(strip(FRAMES_1[0]))
    assert (frame.status, frame.values) == ('no_layout', None)
    assert str(frame) == '[frame 1 seq=0 millis=976335 no_layout] b6f39d3f0800000001'


def test_layout_mismatch():
    lines = ['__LAYOUT__v1 1.0/2 pos:f@5f', '__LAYOUT__v1 1.1/2 state:i@4a', FRAMES_1[0]]  # payload has the bool
    assert feed(Decoder(), lines)[-1].status == 'layout_mismatch'


def test_unknown_version():
    decoder = Decoder()
    feed(decoder, LAYOUT_1)
    assert decoder.feed('__LAYOUT__v2 1:pos:f32') == Text(None, '__LAYOUT__v2 1:pos:f32')
    assert feed(decoder, FRAMES_1)[-1].status == 'unknown_version'
    assert feed(decoder, FRAMES_1, sender=2)[-1].status == 'no_layout'  # versions are kept per sender
    assert feed(decoder, LAYOUT_1 + FRAMES_1)[-1].status == 'ok'


def test_another_version_drops_the_layouts():
    decoder = Decoder()
    feed(decoder, LAYOUT_1 + FRAMES_1[:1])
    decoder.feed('__LAYOUT__v2 1:pos:f32')
    assert feed(decoder, LAYOUT_1[:1] + FRAMES_1[1:])[-1].status == 'layout_incomplete'


def test_bus_prefix():
    decoder = Decoder()
    results = [decoder.feed(f'bus[2]: {strip(line)}') for line in LAYOUT_1 + FRAMES_1]
    assert results[0] == Layout(2, 1, 0, 3, 'pos', 'f')
    assert (results[-1].sender, results[-1].status) == (2, 'ok')
    assert decoder.feed(strip(FRAMES_1[0])).status == 'no_layout'  # layouts are kept per sender
    assert decoder.feed('bus[2]: hello') == Text(None, 'bus[2]: hello')
    assert decoder.feed(f'bus[3]: {strip(FRAMES_1[0])}', sender='core').sender == ('core', 3)


@pytest.mark.parametrize('line', [
    '~hello',
    '"~hello"',  # what core.print("~hello") sends
    '~',
    flip_bit(FRAMES_1[0]),  # wrong CRC
    '~AQDP5Q4AtvOdPwgAAAABTJA',  # length not a multiple of 4
    '~AQDP5Q4AtvOd!PwgAAAABTJA=',  # not in the Base64 alphabet
    '~AQDP5Q4AtvOdPwgAAAABTJA=ä',
    with_crc(bytes([1, 0, 0, 0, 0])),  # valid CRC, but shorter than a header
    'bus[2]: ~hello',
    '__LAYOUT__v1 1.3/3 pos:f',  # index beyond the count
    '__LAYOUT__v1 1.0/3 pos:d',  # unknown type
])
def test_lines_that_are_not_telemetry_stay_text(line):
    decoder = Decoder()
    feed(decoder, LAYOUT_1)
    assert decoder.feed(line) == Text(None, line)


def test_checksum():
    assert telemetry.split_checksum(FRAMES_1[0]) == (FRAMES_1[0][:-3], 0x22)
    assert telemetry.checksum(FRAMES_1[0][:-3]) == 0x22
    assert telemetry.split_checksum('Ready.') == ('Ready.', None)
    assert telemetry.split_checksum('mail me@zz') == ('mail me@zz', None)
