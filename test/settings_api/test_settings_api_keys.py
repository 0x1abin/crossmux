#!/usr/bin/env python3
"""Keep base settings keys unique and reserved capacity equal to the entry count."""

from collections import Counter
from itertools import product
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
SETTINGS_LIST = ROOT / "src/SettingsList.h"


def matching_delimiter(source, opening, opening_char, closing_char):
    """Return the matching delimiter while ignoring C++ string literals."""
    depth = 0
    in_string = False
    escaped = False
    for index in range(opening, len(source)):
        char = source[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == opening_char:
            depth += 1
        elif char == closing_char:
            depth -= 1
            if depth == 0:
                return index
    raise ValueError(f"unclosed {opening_char} at {opening}")


def split_arguments(arguments):
    """Split a C++ call's arguments without splitting nested initializers."""
    parts = []
    start = 0
    stack = []
    in_string = False
    escaped = False
    pairs = {"(": ")", "{": "}", "[": "]"}
    for index, char in enumerate(arguments):
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char in pairs:
            stack.append(pairs[char])
        elif stack and char == stack[-1]:
            stack.pop()
        elif char == "," and not stack:
            parts.append(arguments[start:index].strip())
            start = index + 1
    parts.append(arguments[start:].strip())
    return parts


def base_list_source(source):
    marker = "baseList = [] {"
    start = source.index(marker) + len(marker) - 1
    end = matching_delimiter(source, start, "{", "}")
    return source[start + 1 : end]


def preprocess_capability_guards(source, capabilities):
    """Keep the active branch for the simple feature guards in the base list."""
    result = []
    active = True
    branches = []
    for line in source.splitlines(keepends=True):
        directive = line.strip()
        if directive.startswith("#if "):
            capability = directive.removeprefix("#if ").strip()
            if capability not in capabilities:
                raise ValueError(f"unexpected base-list capability guard: {capability}")
            branches.append((active, capabilities[capability]))
            active = active and capabilities[capability]
        elif directive == "#else":
            parent_active, condition = branches[-1]
            active = parent_active and not condition
        elif directive == "#endif":
            active = branches.pop()[0]
        elif active:
            result.append(line)
    if branches:
        raise ValueError("unclosed base-list capability guard")
    return "".join(result)


def setting_keys(source, capabilities):
    key_argument_index = {
        "Toggle": 2,
        "Enum": 3,
        "StaticEnum": 3,
        "Value": 3,
        "SignedValue": 3,
        "String": 3,
        "DynamicEnum": 4,
        "DynamicString": 3,
    }
    keys = []
    list_source = preprocess_capability_guards(base_list_source(source), capabilities)
    position = 0
    while True:
        start = list_source.find("SettingInfo::", position)
        if start < 0:
            return keys
        name_start = start + len("SettingInfo::")
        opening = list_source.find("(", name_start)
        method = list_source[name_start:opening]
        closing = matching_delimiter(list_source, opening, "(", ")")
        position = closing + 1
        if method not in key_argument_index:
            continue
        argument = split_arguments(list_source[opening + 1 : closing])[key_argument_index[method]]
        if argument.startswith('"') and argument.endswith('"'):
            keys.append(argument[1:-1])


def main():
    source = SETTINGS_LIST.read_text()
    capacity_checks = ["#include <cstddef>"]
    capability_names = (
        "FREEINK_CAP_FRONTLIGHT",
        "FREEINK_CAP_TOUCH",
        "CROSSPOINT_CAP_SOUND_FEEDBACK",
        "FREEINK_CAP_HAPTIC",
        "FREEINK_CAP_WARMLIGHT",
    )
    for values in product((False, True), repeat=len(capability_names)):
        capabilities = dict(zip(capability_names, values))
        keys = setting_keys(source, capabilities)
        duplicates = sorted(key for key, count in Counter(keys).items() if count > 1)
        assert not duplicates, (
            f"duplicate Web Settings API keys for {capabilities}: {', '.join(duplicates)}"
        )
        entries = preprocess_capability_guards(base_list_source(source), capabilities)
        entry_count = len(re.findall(r"SettingInfo::\w+\(", entries))
        batch_sizes = []
        for batch in re.finditer(r"v\.insert\(v\.end\(\), \{", entries):
            opening = batch.end() - 1
            closing = matching_delimiter(entries, opening, "{", "}")
            batch_sizes.append(len(re.findall(r"SettingInfo::\w+\(", entries[opening:closing])))
        assert sum(batch_sizes) == entry_count, f"settings outside insert batches for {capabilities}"
        assert batch_sizes and max(batch_sizes) <= 4, f"oversized settings batch for {capabilities}: {batch_sizes}"
        declaration = re.search(r"constexpr size_t entryCount = [^;]+;", entries).group()
        capacity_checks.append(
            f"namespace combination_{len(capacity_checks)} {{ {declaration} "
            f"static_assert(entryCount == {entry_count}); }}"
        )
    subprocess.run(
        ["c++", "-std=c++20", "-x", "c++", "-fsyntax-only", "-"],
        input="\n".join(capacity_checks), text=True, check=True,
    )
    print(f"Web Settings API keys are unique across {2 ** len(capability_names)} capability combinations")
    print("Base settings capacity matches every capability combination without vector growth")
    print("Every settings initializer batch has at most four entries")


if __name__ == "__main__":
    main()
