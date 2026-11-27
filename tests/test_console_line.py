from lizard_host import checksum, format_line, split_checksum


def test_checksum():
    assert split_checksum('~AQkAkw0AALbznT8IAAAAAeQo@6d') == ('~AQkAkw0AALbznT8IAAAAAeQo', 0x6d)
    assert checksum('~AQkAkw0AALbznT8IAAAAAeQo') == 0x6d
    assert split_checksum('Ready.') == ('Ready.', None)
    assert split_checksum('mail me@zz') == ('mail me@zz', None)


def test_line_end():
    assert split_checksum('~AQkAkw0AALbznT8IAAAAAeQo@6d\r\n') == ('~AQkAkw0AALbznT8IAAAAAeQo', 0x6d)
    assert split_checksum('Ready.\r\n') == ('Ready.', None)


def test_format_line():
    assert format_line('core.telemetry_info()') == f'core.telemetry_info()@{checksum("core.telemetry_info()"):02x}\n'
    assert split_checksum(format_line('core.millis')) == ('core.millis', checksum('core.millis'))
