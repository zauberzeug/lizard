import base64
import binascii
import struct

import pytest

from lizard_host import Frame, Layout, TelemetryDecoder, Text, checksum, split_checksum

# Recorded from an ESP32 after
#   float pos = 1.234, int state = 8, bool on = true, bool b0 = true ... bool b8 = false (true every third),
#   float neg = -1e30, int low = -2147483648
# core.telemetry(pos, state, on, 100)
LAYOUT_1 = ['__LAYOUT__v1 1.0/3 09 pos:f@77', '__LAYOUT__v1 1.1/3 09 state:i@62', '__LAYOUT__v1 1.2/3 09 on:?@41']
FRAMES_1 = ['~AQkAkw0AALbznT8IAAAAAeQo@6d', '~AQkB/Q0AALbznT8IAAAAAQyg@18']
# core.telemetry(core.millis, neg, low, b0, b1, b2, b3, b4, b5, b6, b7, b8, 250)
LAYOUT_2 = [
    '__LAYOUT__v1 2.0/12 ee core.millis:i@05', '__LAYOUT__v1 2.1/12 ee neg:f@4c', '__LAYOUT__v1 2.2/12 ee low:i@58',
    '__LAYOUT__v1 2.3/12 ee b0:?@29', '__LAYOUT__v1 2.4/12 ee b1:?@2f', '__LAYOUT__v1 2.5/12 ee b2:?@2d',
    '__LAYOUT__v1 2.6/12 ee b3:?@2f', '__LAYOUT__v1 2.7/12 ee b4:?@29', '__LAYOUT__v1 2.8/12 ee b5:?@27',
    '__LAYOUT__v1 2.9/12 ee b6:?@25', '__LAYOUT__v1 2.10/12 ee b7:?@1c', '__LAYOUT__v1 2.11/12 ee b8:?@12',
]
FRAME_2 = '~Au4AFQ8AABUPAADK8knxAAAAgEkATm8=@29'
# the last frames before core.telemetry_info(), and what follows while both frames run
BEFORE_REPEAT = ['~Au4F+hMAAPoTAADK8knxAAAAgEkAEqk=@7d', '~AQkRQBQAALbznT8IAAAAAZ6M@6a', '~AQkSpBQAALbznT8IAAAAAYjG@1f']
REPEAT = [
    '__LAYOUT__v1 1.0/3 09 pos:f@77', '__LAYOUT__v1 1.1/3 09 state:i@62', '__LAYOUT__v1 1.2/3 09 on:?@41',
    '__LAYOUT__v1 2.0/12 ee core.millis:i@05', '__LAYOUT__v1 2.1/12 ee neg:f@4c', '__LAYOUT__v1 2.2/12 ee low:i@58',
    '__LAYOUT__v1 2.3/12 ee b0:?@29', '__LAYOUT__v1 2.4/12 ee b1:?@2f', '__LAYOUT__v1 2.5/12 ee b2:?@2d',
    '__LAYOUT__v1 2.6/12 ee b3:?@2f', '__LAYOUT__v1 2.7/12 ee b4:?@29', '__LAYOUT__v1 2.8/12 ee b5:?@27',
    '~Au4G9BQAAPQUAADK8knxAAAAgEkAM3I=@0f',
    '__LAYOUT__v1 2.9/12 ee b6:?@25', '__LAYOUT__v1 2.10/12 ee b7:?@1c', '__LAYOUT__v1 2.11/12 ee b8:?@12',
    '~AQkTCBUAALbznT8IAAAAAaMo@18',
]
# core.clear_telemetry() and core.telemetry(pos, on, state, 100)
REDEFINED = [
    '__LAYOUT__v1 1.0/3 85 pos:f@73', '__LAYOUT__v1 1.1/3 85 on:?@46', '__LAYOUT__v1 1.2/3 85 state:i@65',
    '~AYUADxgAALbznT8IAAAAARNU@3e',
]
# core.clear_telemetry() and core.telemetry(pos, state, 100)
SHORTENED = ['__LAYOUT__v1 1.0/2 69 pos:f@70', '__LAYOUT__v1 1.1/2 69 state:i@65', '~AWkAdxkAALbznT8IAAAA+8U=@51']


def feed(decoder: TelemetryDecoder, lines: list, sender=None) -> list:
    results = []
    for line in lines:
        text, check = split_checksum(line)
        assert check is None or check == checksum(text)
        results.append(decoder.feed(text, sender))
    return results


def strip(line: str) -> str:
    return split_checksum(line)[0]


def with_crc(body: bytes) -> str:
    return '~' + base64.b64encode(body + binascii.crc_hqx(body, 0xffff).to_bytes(2, 'little')).decode()


def encode(frame_id: int, layout_id: int, seq: int, millis: int, payload: bytes) -> str:
    return with_crc(struct.pack('<BBBI', frame_id, layout_id, seq, millis) + payload)


def with_seq(line: str, seq: int) -> str:
    body = bytearray(base64.b64decode(strip(line)[1:])[:-2])
    body[2] = seq
    return with_crc(bytes(body))


def flip_bit(line: str) -> str:
    body = bytearray(base64.b64decode(strip(line)[1:]))
    body[7] ^= 0x01
    return '~' + base64.b64encode(body).decode()


def f32(value: float) -> float:
    return struct.unpack('<f', struct.pack('<f', value))[0]


def test_recorded_frame():
    *layouts, frame = feed(TelemetryDecoder(), LAYOUT_1 + FRAMES_1[:1])
    assert layouts == [Layout(None, 1, 0x09, 0, 3, 'pos', 'f'), Layout(None, 1, 0x09, 1, 3, 'state', 'i'),
                       Layout(None, 1, 0x09, 2, 3, 'on', '?')]
    assert (frame.frame_id, frame.layout_id, frame.seq, frame.millis, frame.status) == (1, 0x09, 0, 3475, 'ok')
    assert frame.values == {'pos': f32(1.234), 'state': 8, 'on': True}
    assert str(frame) == '[frame 1 seq=0 millis=3475] pos=1.234 state=8 on=true'


def test_recorded_frame_with_nine_bools():
    frame = feed(TelemetryDecoder(), LAYOUT_2 + [FRAME_2])[-1]
    assert frame.status == 'ok'
    assert frame.values['core.millis'] == frame.millis
    assert frame.values['neg'] == f32(-1e30)
    assert frame.values['low'] == -2**31
    assert [frame.values[f'b{i}'] for i in range(9)] == [i % 3 == 0 for i in range(9)]
    assert str(frame).endswith(' neg=-1e+30 low=-2147483648 b0=true b1=false b2=false b3=true b4=false b5=false '
                               'b6=true b7=false b8=false')


def test_documented_example():
    lines = ['__LAYOUT__v1 1.0/3 bf motor.position:f@4a', '__LAYOUT__v1 1.1/3 bf motor.axis_state:i@76',
             '__LAYOUT__v1 1.2/3 bf motor.enabled:?@6d', '~Ab8HQOIBALbznT8IAAAAAY/t@25']
    assert str(feed(TelemetryDecoder(), lines)[-1]) == \
        '[frame 1 seq=7 millis=123456] motor.position=1.234 motor.axis_state=8 motor.enabled=true'


@pytest.mark.parametrize('lines', [LAYOUT_1, LAYOUT_2, REDEFINED[:3], SHORTENED[:2]])
def test_layout_id_is_a_hash_of_the_fields(lines):
    layouts = feed(TelemetryDecoder(), lines)
    fields = ' '.join(f'{layout.name}:{layout.type}' for layout in layouts)
    assert {layout.layout_id for layout in layouts} == {binascii.crc_hqx(fields.encode(), 0xffff) & 0xff}


def test_frames_keep_decoding_while_layout_lines_repeat():
    results = feed(TelemetryDecoder(), LAYOUT_1 + LAYOUT_2 + BEFORE_REPEAT + REPEAT)
    assert [result.status for result in results if isinstance(result, Frame)] == ['ok'] * 5


def test_reused_frame_id():
    results = feed(TelemetryDecoder(), LAYOUT_1 + FRAMES_1 + REDEFINED + SHORTENED)
    assert list(results[-4].values) == ['pos', 'on', 'state']
    assert results[-1].values == {'pos': f32(1.234), 'state': 8}


@pytest.mark.parametrize('layout_lines, status', [
    ([], 'no_layout'),  # all layout lines of the new fields are lost
    (REDEFINED[:1], 'layout_incomplete'),  # the lines that differ from the old layout are lost
])
def test_reused_frame_id_is_never_decoded_with_the_old_layout(layout_lines, status):
    decoder = TelemetryDecoder()
    feed(decoder, LAYOUT_1 + FRAMES_1 + layout_lines)
    assert decoder.feed(with_seq(REDEFINED[-1], 2)).status == status  # although the sequence continues the old one
    assert feed(decoder, REDEFINED)[-1].status == 'ok'


@pytest.mark.parametrize('line', [
    '__LAYOUT__v1 1.1/3 09 on:?',  # a differing line
    '__LAYOUT__v1 1.0/2 09 pos:f',  # a differing count
])
def test_other_fields_with_the_same_layout_id_drop_the_old_layout(line):
    decoder = TelemetryDecoder()  # the new fields get the same layout ID by chance
    feed(decoder, LAYOUT_1)
    decoder.feed(line)
    assert decoder.feed(strip(FRAMES_1[0])).status == 'layout_incomplete'


@pytest.mark.parametrize('lines', [
    LAYOUT_1[:2] + REDEFINED[:1] + REDEFINED[2:],  # lines of the old layout, then some of the new one
    SHORTENED[1:2] + REDEFINED[1:],  # lines of a layout with another count
])
def test_lines_of_an_older_layout_are_not_mixed_into_a_new_one(lines):
    assert feed(TelemetryDecoder(), lines)[-1].status == 'layout_incomplete'


def test_lost_last_layout_line():
    decoder = TelemetryDecoder()
    assert feed(decoder, LAYOUT_2[:-1] + [FRAME_2])[-1].status == 'layout_incomplete'
    assert feed(decoder, LAYOUT_2[-1:] + [FRAME_2])[-1].status == 'ok'


def test_lost_last_bool_layout_line_sharing_its_byte():
    decoder = TelemetryDecoder()
    decoder.feed('__LAYOUT__v1 1.0/2 5a a:?')  # the line of b is lost; a and b share one byte of the payload
    frame = decoder.feed(encode(1, 0x5a, 0, 0, bytes([0b11])))
    assert (frame.status, frame.values) == ('layout_incomplete', None)


def test_no_layout():
    frame = TelemetryDecoder().feed(strip(FRAMES_1[0]))
    assert (frame.status, frame.values) == ('no_layout', None)
    assert str(frame) == '[frame 1 seq=0 millis=3475 no_layout] b6f39d3f0800000001'


def test_layout_mismatch():
    lines = ['__LAYOUT__v1 1.0/2 09 pos:f', '__LAYOUT__v1 1.1/2 09 state:i', FRAMES_1[0]]  # payload has the bool
    assert feed(TelemetryDecoder(), lines)[-1].status == 'layout_mismatch'


def test_unknown_version():
    decoder = TelemetryDecoder()
    feed(decoder, LAYOUT_1)
    assert decoder.feed('__LAYOUT__v2 1:pos:f32') == Text(None, '__LAYOUT__v2 1:pos:f32')
    assert feed(decoder, FRAMES_1)[-1].status == 'unknown_version'
    assert feed(decoder, FRAMES_1, sender=2)[-1].status == 'no_layout'  # versions are kept per sender
    assert feed(decoder, LAYOUT_1 + FRAMES_1)[-1].status == 'ok'


def test_another_version_drops_the_layouts():
    decoder = TelemetryDecoder()
    feed(decoder, LAYOUT_1 + FRAMES_1[:1])
    decoder.feed('__LAYOUT__v2 1:pos:f32')
    assert feed(decoder, LAYOUT_1[:1] + FRAMES_1[1:])[-1].status == 'layout_incomplete'


def test_bus_prefix():
    decoder = TelemetryDecoder()
    results = [decoder.feed(f'bus[2]: {strip(line)}') for line in LAYOUT_1 + FRAMES_1]
    assert results[0] == Layout(2, 1, 0x09, 0, 3, 'pos', 'f')
    assert (results[-1].sender, results[-1].status) == (2, 'ok')
    assert decoder.feed(strip(FRAMES_1[0])).status == 'no_layout'  # layouts are kept per sender
    assert decoder.feed('bus[2]: hello') == Text(None, 'bus[2]: hello')
    assert decoder.feed(f'bus[3]: {strip(FRAMES_1[0])}', sender='core').sender == ('core', 3)


@pytest.mark.parametrize('line', [
    '~hello',
    '"~hello"',  # what core.print("~hello") sends
    '~',
    flip_bit(FRAMES_1[0]),  # wrong CRC
    '~AQkAkw0AALbznT8IAAAAAeQ',  # length not a multiple of 4
    '~AQkAkw0AALbz!nT8IAAAAAeQo',  # not in the Base64 alphabet
    '~AQkAkw0AALbznT8IAAAAAeQoä',
    with_crc(bytes([1, 9, 0, 0, 0])),  # valid CRC, but shorter than a header
    'bus[2]: ~hello',
    '__LAYOUT__v1 1.3/3 09 pos:f',  # index beyond the count
    '__LAYOUT__v1 1.0/3 09 pos:d',  # unknown type
    '__LAYOUT__v1 1.0/3 pos:f',  # no layout ID
])
def test_lines_that_are_not_telemetry_stay_text(line):
    decoder = TelemetryDecoder()
    feed(decoder, LAYOUT_1)
    assert decoder.feed(line) == Text(None, line)
