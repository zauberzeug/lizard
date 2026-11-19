from pytest import approx

from lizard_host import ClockOffset, parse_core_line


def test_parse_core_line():
    line = parse_core_line('core 12345 1.500 true')
    assert line is not None and (line.millis, line.values) == (12345, ['12345', '1.500', 'true'])
    assert parse_core_line('core') is not None and parse_core_line('core').millis is None
    assert parse_core_line('bus[1]: core 1') is None


def test_clock_offset():
    offset = ClockOffset()
    assert offset.host_time(1000) is None
    assert offset.update(101.0, 1000) == 100.0
    assert offset.update(102.02, 2000) == approx(100.01)
    assert offset.host_time(3000) == approx(103.01)
    assert offset.update(50.0, 3000) == 47.0  # a restart starts anew
