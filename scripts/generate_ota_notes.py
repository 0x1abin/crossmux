#!/usr/bin/env python3
"""Validate and serialize structured OTA release notes."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


FORBIDDEN = set("`*_#<>[]{}\\")
MARKER_PREFIX = "<!-- OTA_NOTES "
MARKER_PATTERN = re.compile(r"<!-- OTA_NOTES ([^\r\n]+) -->")
MIN_NOTES = 2
MAX_NOTES = 8
MAX_NOTE_BYTES = 96


def validate_note(value: object, language: str) -> str:
    if not isinstance(value, str) or value != value.strip() or not value:
        raise ValueError(f"invalid {language} release note")
    if any(ord(char) < 0x20 or ord(char) == 0x7F or char in FORBIDDEN for char in value):
        raise ValueError(f"unsafe {language} release note")
    if len(value.encode("utf-8")) > MAX_NOTE_BYTES:
        raise ValueError(f"{language} release note exceeds {MAX_NOTE_BYTES} bytes")
    if language == "en" and not value.isascii():
        raise ValueError("English release notes must be ASCII")
    if language == "zh" and len(value) > 32:
        raise ValueError("Chinese release note exceeds 32 characters")
    return value


def validate_notes(value: object) -> dict[str, list[str]]:
    if not isinstance(value, dict) or set(value) != {"en", "zh"}:
        raise ValueError("release notes must contain only en and zh")
    result: dict[str, list[str]] = {}
    for language in ("en", "zh"):
        notes = value[language]
        if not isinstance(notes, list) or not MIN_NOTES <= len(notes) <= MAX_NOTES:
            raise ValueError(f"{language} release notes must contain {MIN_NOTES}-{MAX_NOTES} items")
        result[language] = [validate_note(note, language) for note in notes]
        if len(set(result[language])) != len(result[language]):
            raise ValueError(f"{language} release notes must be unique")
    if len(result["en"]) != len(result["zh"]):
        raise ValueError("English and Chinese release notes must contain the same number of items")
    return result


def parse_multiline(value: str | None, language: str) -> list[str]:
    if value is None or "\r" in value:
        raise ValueError(f"invalid {language} multiline release notes")
    return value.split("\n")


def extract_notes(value: str) -> dict[str, list[str]]:
    if value.count(MARKER_PREFIX) != 1:
        raise ValueError("tag must contain exactly one OTA notes marker")
    match = MARKER_PATTERN.search(value)
    if not match:
        raise ValueError("invalid OTA notes marker")
    try:
        return validate_notes(json.loads(match.group(1)))
    except json.JSONDecodeError as error:
        raise ValueError("invalid OTA notes JSON") from error


def marker(notes: dict[str, list[str]]) -> str:
    compact = json.dumps(notes, ensure_ascii=False, separators=(",", ":"))
    return f"<!-- OTA_NOTES {compact} -->\n"


def rejects(value: object) -> None:
    try:
        validate_notes(value)
    except ValueError:
        return
    raise AssertionError(f"accepted invalid release notes: {value}")


def self_test() -> None:
    valid = {
        "en": ["Faster book opening", "More reliable OTA updates", "New font sizes 20 and 22"],
        "zh": ["加快图书打开速度", "提升OTA更新可靠性", "新增20和22号字体"],
    }
    assert validate_notes(valid) == valid
    valid_marker = marker(valid)
    assert extract_notes(f"CrossMux release\n\n{valid_marker}") == valid
    assert validate_notes({"en": valid["en"][:2], "zh": valid["zh"][:2]})
    assert validate_notes(
        {
            "en": [f"English note {index}" for index in range(MAX_NOTES)],
            "zh": [f"中文更新{index}" for index in range(MAX_NOTES)],
        }
    )
    rejects({"en": ["one"], "zh": ["一"]})
    rejects({"en": ["one", "two"], "zh": ["一", "二", "三"]})
    rejects({"en": ["x" * 97, "two"], "zh": ["一", "二"]})
    rejects({"en": ["bad * markdown", "two"], "zh": ["一", "二"]})
    rejects({"en": ["bad\x01control", "two"], "zh": ["一", "二"]})
    rejects({"en": ["same", "same"], "zh": ["一", "二"]})
    rejects({"en": ["one", "two"], "zh": ["中" * 33, "正常"]})
    rejects({"en": [f"note {index}" for index in range(MAX_NOTES + 1)], "zh": ["一"] * 9})
    for value in (
        "CrossMux release",
        "<!-- OTA_NOTES { -->",
        valid_marker + valid_marker,
        '<!-- OTA_NOTES {"en":["line\none","two"],"zh":["一","二"]} -->',
    ):
        try:
            extract_notes(value)
        except ValueError:
            pass
        else:
            raise AssertionError(f"accepted invalid marker: {value}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input")
    parser.add_argument("--en-notes")
    parser.add_argument("--zh-notes")
    parser.add_argument("--output", default="ota-notes.md")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        print("OTA release notes self-test passed")
        return 0

    output = Path(args.output)
    output.unlink(missing_ok=True)
    try:
        if args.input and (args.en_notes is not None or args.zh_notes is not None):
            raise ValueError("use either --input or direct release notes")
        if args.input:
            notes = extract_notes(Path(args.input).read_text(encoding="utf-8"))
        else:
            notes = validate_notes(
                {
                    "en": parse_multiline(args.en_notes, "en"),
                    "zh": parse_multiline(args.zh_notes, "zh"),
                }
            )
        output.write_text(marker(notes), encoding="utf-8")
    except Exception as error:
        print(f"::warning::OTA release notes omitted: {error}", file=sys.stderr)
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
