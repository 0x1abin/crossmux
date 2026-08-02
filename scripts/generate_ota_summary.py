#!/usr/bin/env python3
"""Validate an OTA summary supplied in an annotated release tag."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


FORBIDDEN = set("`*_#<>[]{}\\")
MARKER_PREFIX = "<!-- OTA_SUMMARY "
MARKER_PATTERN = re.compile(r"<!-- OTA_SUMMARY ([^\r\n]+) -->")


def validate_line(value: object, language: str) -> str:
    if not isinstance(value, str) or value != value.strip() or not value:
        raise ValueError(f"invalid {language} summary line")
    if any(ord(char) < 0x20 or ord(char) == 0x7F or char in FORBIDDEN for char in value):
        raise ValueError(f"unsafe {language} summary line")
    if language == "en":
        if not value.isascii() or len(value) > 42:
            raise ValueError("English summary exceeds 42 ASCII characters")
    elif len(value) > 16 or len(value.encode("utf-8")) > 48:
        raise ValueError("Chinese summary exceeds 16 characters")
    return value


def validate_summary(value: object) -> dict[str, list[str]]:
    if not isinstance(value, dict) or set(value) != {"en", "zh"}:
        raise ValueError("summary must contain only en and zh")
    result: dict[str, list[str]] = {}
    for language in ("en", "zh"):
        lines = value[language]
        if not isinstance(lines, list) or len(lines) != 2:
            raise ValueError(f"{language} summary must contain two lines")
        result[language] = [validate_line(line, language) for line in lines]
        if result[language][0] == result[language][1]:
            raise ValueError(f"{language} summary lines must differ")
    return result


def extract_summary(value: str) -> dict[str, list[str]]:
    if value.count(MARKER_PREFIX) != 1:
        raise ValueError("tag must contain exactly one OTA summary marker")
    match = MARKER_PATTERN.search(value)
    if not match:
        raise ValueError("invalid OTA summary marker")
    try:
        return validate_summary(json.loads(match.group(1)))
    except json.JSONDecodeError as error:
        raise ValueError("invalid OTA summary JSON") from error


def marker(summary: dict[str, list[str]]) -> str:
    compact = json.dumps(summary, ensure_ascii=False, separators=(",", ":"))
    return f"<!-- OTA_SUMMARY {compact} -->\n"


def self_test() -> None:
    valid = {
        "en": ["Faster book opening", "More reliable OTA updates"],
        "zh": ["加快图书打开速度", "提升OTA更新可靠性"],
    }
    assert validate_summary(valid) == valid
    valid_marker = marker(valid)
    assert extract_summary(f"CrossMux release\n\n{valid_marker}") == valid
    invalid = [
        {"en": ["one"], "zh": valid["zh"]},
        {"en": ["x" * 43, "two"], "zh": valid["zh"]},
        {"en": ["bad * markdown", "two"], "zh": valid["zh"]},
        {"en": ["bad\x01control", "two"], "zh": valid["zh"]},
        {"en": ["same", "same"], "zh": valid["zh"]},
        {"en": valid["en"], "zh": []},
        {"en": valid["en"], "zh": ["中" * 17, "正常"]},
    ]
    for value in invalid:
        try:
            validate_summary(value)
        except ValueError:
            pass
        else:
            raise AssertionError(f"accepted invalid summary: {value}")
    invalid_markers = [
        "CrossMux release",
        "<!-- OTA_SUMMARY { -->",
        valid_marker + valid_marker,
        '<!-- OTA_SUMMARY {"en": ["line\none", "two"], "zh": ["一", "二"]} -->',
    ]
    for value in invalid_markers:
        try:
            extract_summary(value)
        except ValueError:
            pass
        else:
            raise AssertionError(f"accepted invalid marker: {value}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input")
    parser.add_argument("--output", default="ota-summary.md")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        print("OTA summary self-test passed")
        return 0

    output = Path(args.output)
    output.unlink(missing_ok=True)
    try:
        if not args.input:
            raise ValueError("input file is required")
        summary = extract_summary(Path(args.input).read_text(encoding="utf-8"))
        output.write_text(marker(summary), encoding="utf-8")
    except Exception as error:
        print(f"::warning::OTA summary omitted: {error}", file=sys.stderr)
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
