from lizard_host import LizardVersion
from lizard_host.version import parse_info, parse_info_line, required_features


def test_parse():
    assert LizardVersion.parse('v0.16.0').release == (0, 16, 0)
    version = LizardVersion.parse('v0.15.1-40-gea96593-dirty')
    assert (version.release, version.commits_after, version.dirty) == ((0, 15, 1), 40, True)
    assert LizardVersion.parse('unknown').release is None
    assert LizardVersion.parse('ea96593').release is None


def test_supports():
    assert LizardVersion.parse('v0.16.0').supports((0, 16, 0)) == 'yes'
    assert LizardVersion.parse('v0.16.1-3-gabcdef0').supports((0, 16, 0)) == 'yes'
    assert LizardVersion.parse('v0.15.1').supports((0, 16, 0)) == 'no'
    assert LizardVersion.parse('v0.15.1-40-gea96593').supports((0, 16, 0)) == 'unsure'
    assert LizardVersion.parse('v0.15.1-dirty').supports((0, 16, 0)) == 'unsure'
    assert LizardVersion.parse('unknown').supports((0, 16, 0)) == 'unsure'


def test_parse_info():
    assert parse_info({'project name': 'lizard', 'project version': 'v0.16.0'}) == LizardVersion.parse('v0.16.0')
    assert parse_info({'project name': 'my-robot', 'project version': 'v0.2.0', 'lizard version': 'v0.16.0'}) == \
        LizardVersion.parse('v0.16.0')
    assert parse_info({'project name': 'lizard', 'version': 'v0.15.1'}) == LizardVersion.parse('v0.15.1')
    assert parse_info({'project name': 'lizard-zz', 'version': 'v0.2.0'}) is None  # before 0.16.0


def test_parse_info_line():
    assert parse_info_line('lizard version: v0.16.0') == ('lizard version', 'v0.16.0')
    assert parse_info_line('compile time: Nov 13 2026, 12:40:50') == ('compile time', 'Nov 13 2026, 12:40:50')
    assert parse_info_line('core 1234 5') is None


def test_required_features():
    assert [f.release for f in required_features('core.telemetry(1, a.b)\n')] == [(0, 16, 0)]
    assert required_features('# core.telemetry(1, a.b)\nled = Output(15)\n') == []
