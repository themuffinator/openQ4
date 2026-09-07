#!/usr/bin/env python3
"""Contract checks for the competitive match-rule localization surface."""

from __future__ import annotations

from collections import Counter
import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")
).resolve()
STRINGS_ROOT = ROOT / "content" / "baseoq4" / "pak0" / "strings"
LANGUAGE_FILES = (
    "english_openq4.lang",
    "french_openq4.lang",
    "italian_openq4.lang",
    "spanish_openq4.lang",
)
# 41693-41698 are the three built-in match-series profiles (label, description)
# consumed by seriesProfileDescriptors in mp/match/MatchSeries.cpp.
EXPECTED_IDS = tuple(f"#str_{value}" for value in range(41600, 41699)) + ("#str_42680", "#str_42681")
EXPECTED_SET = set(EXPECTED_IDS)
DESCRIPTION_IDS = (
    {f"#str_{value}" for value in range(41601, 41666, 2)}
    | {f"#str_{value}" for value in range(41678, 41693, 2)}
    | {f"#str_{value}" for value in range(41694, 41699, 2)}
    | {"#str_42681"}
)
ENTRY_RE = re.compile(r'^\s*"(?P<id>#str_\d+)"\s+"(?P<value>.*)"\s*$')
RULE_ID_RE = re.compile(r'"(?P<id>#str_(?:416\d{2}|4268[01]))"')
PLACEHOLDER_RE = re.compile(
    r"(?:#str_|\b(?:todo|tbd|fixme|placeholder|missing translation)\b|\?\?\?)",
    re.IGNORECASE,
)


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def parse_language_table(path: Path) -> dict[str, str]:
    entries: list[tuple[str, str]] = []
    for line in read(path).splitlines():
        match = ENTRY_RE.match(line)
        if match is None:
            continue
        entries.append((match.group("id"), match.group("value")))

    counts = Counter(identifier for identifier, _ in entries)
    duplicates = sorted(identifier for identifier, count in counts.items() if count != 1)
    if duplicates:
        raise AssertionError(
            f"{path.name} contains duplicate string IDs: {', '.join(duplicates)}"
        )

    table = dict(entries)
    actual_target_ids = set(table) & {
        f"#str_{value}" for value in range(41600, 41700)
    } | (set(table) & {"#str_42680", "#str_42681"})
    if actual_target_ids != EXPECTED_SET:
        missing = sorted(EXPECTED_SET - actual_target_ids)
        unexpected = sorted(actual_target_ids - EXPECTED_SET)
        raise AssertionError(
            f"{path.name} competitive ID mismatch; missing={missing}, "
            f"unexpected={unexpected}"
        )

    for identifier in EXPECTED_IDS:
        value = table[identifier].strip()
        if not value:
            raise AssertionError(f"{path.name} has an empty value for {identifier}")
        if PLACEHOLDER_RE.search(value):
            raise AssertionError(
                f"{path.name} has placeholder text for {identifier}: {value!r}"
            )
        if identifier in DESCRIPTION_IDS and len(value) < 12:
            raise AssertionError(
                f"{path.name} has an incomplete description for {identifier}: {value!r}"
            )

    return {identifier: table[identifier] for identifier in EXPECTED_IDS}


def validate_rule_references() -> None:
    # The rule table owns most of the range; the three built-in series profiles
    # are declared next to the profiles themselves in MatchSeries.cpp.  Both are
    # checked together so every string in the range has exactly one consumer.
    match_dir = GAME_ROOT / "src" / "mpgame" / "mp" / "match"
    sources = ("MatchRules.cpp", "MatchSeries.cpp")
    references: list[str] = []
    for source in sources:
        references.extend(RULE_ID_RE.findall(read(match_dir / source)))
    label = " + ".join(sources)
    counts = Counter(references)
    if set(counts) != EXPECTED_SET:
        missing = sorted(EXPECTED_SET - set(counts))
        unexpected = sorted(set(counts) - EXPECTED_SET)
        raise AssertionError(
            f"{label} localization ID mismatch; missing={missing}, "
            f"unexpected={unexpected}"
        )
    repeated = sorted(identifier for identifier, count in counts.items() if count != 1)
    if repeated:
        raise AssertionError(
            f"{label} must reference each competitive localization ID exactly once: "
            + ", ".join(repeated)
        )


def main() -> None:
    validate_rule_references()
    tables = {
        file_name: parse_language_table(STRINGS_ROOT / file_name)
        for file_name in LANGUAGE_FILES
    }
    english = tables[LANGUAGE_FILES[0]]
    for file_name in LANGUAGE_FILES[1:]:
        if tables[file_name] == english:
            raise AssertionError(f"{file_name} duplicates the complete English block")
    print("competitive_match_localization: ok")


if __name__ == "__main__":
    main()
