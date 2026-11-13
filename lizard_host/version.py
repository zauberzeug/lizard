"""The Lizard version a firmware is built from and the features a startup script needs from it.

``core.info()`` prints ``lizard version: ...`` when the project carries its own name and version,
and ``project version: ...`` otherwise; firmware before 0.16.0 prints ``version: ...``.
"""
import re
from dataclasses import dataclass
from typing import Dict, List, Literal, Optional, Pattern, Tuple

Release = Tuple[int, int, int]
Support = Literal['yes', 'unsure', 'no']

VERSION = re.compile(r'v?(\d+)\.(\d+)\.(\d+)(?:-(\d+)-g[0-9a-f]+)?(-dirty)?')
INFO_LABELS = ('project name', 'lizard version', 'project version', 'version', 'compile time', 'idf version')


@dataclass(frozen=True)
class Feature:
    name: str
    release: Release
    pattern: Pattern[str]  # finds the feature in a startup script


FEATURES: List[Feature] = [
    Feature('telemetry frames (core.telemetry)', (0, 16, 0), re.compile(r'\bcore\.telemetry\s*\(')),
    Feature('passing on telemetry of serial bus peers (pass_telemetry)', (0, 16, 0),
            re.compile(r'\.pass_telemetry\s*\(')),
    Feature('the BusTelemetry module', (0, 16, 0), re.compile(r'\bBusTelemetry\s*\(')),
    Feature('the JbdBms module', (0, 16, 0), re.compile(r'\bJbdBms\s*\(')),
]


@dataclass(frozen=True)
class LizardVersion:
    """A Lizard version as ``git describe`` reports it, e.g. ``v0.16.0`` or ``v0.15.1-40-gea96593``."""
    text: str
    release: Optional[Release]  # None if the text is no Lizard version, e.g. "unknown" or a bare commit hash
    commits_after: int = 0  # commits after the release, i.e. a build between two releases
    dirty: bool = False

    @classmethod
    def parse(cls, text: str) -> 'LizardVersion':
        match = VERSION.fullmatch(text.strip())
        if match is None:
            return cls(text.strip(), None)
        major, minor, patch, commits, dirty = match.groups()
        return cls(text.strip(), (int(major), int(minor), int(patch)), int(commits or 0), dirty is not None)

    def supports(self, release: Release) -> Support:
        """Whether a build of this version has what came with the given release.

        A build between two releases, e.g. v0.15.1-40-gea96593, is newer than v0.15.1 but not v0.16.0,
        so it may or may not have a feature of v0.16.0; the same holds for a build with local changes.
        """
        if self.release is None:
            return 'unsure'
        if self.release >= release:
            return 'yes'
        if self.commits_after or self.dirty:
            return 'unsure'
        return 'no'

    def __str__(self) -> str:
        return self.text


def parse_info(lines: Dict[str, str]) -> Optional[LizardVersion]:
    """Return the Lizard version from the lines of ``core.info()``, keyed by their label.

    Before 0.16.0 the line ``version:`` holds the project's version, which is the Lizard version only for a project
    named "lizard"; for another project the Lizard version is unknown and ``None`` is returned.
    """
    if 'lizard version' in lines:
        return LizardVersion.parse(lines['lizard version'])
    if 'project version' in lines:
        return LizardVersion.parse(lines['project version'])
    if 'version' in lines and lines.get('project name', 'lizard') == 'lizard':
        return LizardVersion.parse(lines['version'])
    return None


def parse_info_line(text: str) -> Optional[Tuple[str, str]]:
    """Split a line of ``core.info()`` into its label and value, or return None for any other line."""
    label, separator, value = text.partition(': ')
    if not separator or label not in INFO_LABELS:
        return None
    return label, value


def required_features(script: str) -> List[Feature]:
    """Return the features a startup script uses that not every Lizard version has."""
    code = '\n'.join(line for line in script.splitlines() if not line.lstrip().startswith('#'))
    return [feature for feature in FEATURES if feature.pattern.search(code)]


def format_release(release: Release) -> str:
    return 'v' + '.'.join(map(str, release))
