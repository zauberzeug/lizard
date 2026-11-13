import sys

from lizard_host import espresso


def test_esptool_command(monkeypatch):
    monkeypatch.setattr(espresso.importlib.util, 'find_spec', lambda name: object())
    assert espresso.esptool_command() == (sys.executable, '-m', 'esptool')
    monkeypatch.setattr(espresso.importlib.util, 'find_spec', lambda name: None)
    assert espresso.esptool_command() == ('esptool.py',)
