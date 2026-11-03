#!/usr/bin/env python3
"""Keep streamed settings keys unique and reserve before collecting descriptors."""

from collections import Counter
from itertools import product
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
SETTINGS_LIST = ROOT / "src/SettingsList.cpp"


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
    marker = "void forEachBaseSetting("
    start = source.index("{", source.index(marker))
    end = matching_delimiter(source, start, "{", "}")
    entries = source[start + 1 : end]
    # Inline the two board-dependent selector factories for metadata/capacity
    # checks. They still contribute one entry each to the production base list.
    for factory in ("buildLongPressMenuSetting", "buildShortPowerButtonSetting"):
        body_start = source.index("{", source.index("inline SettingInfo " + factory + "("))
        body_end = matching_delimiter(source, body_start, "{", "}")
        body = source[body_start + 1 : body_end].strip()
        assert body.startswith("return ") and body.endswith(";")
        entries = entries.replace(factory + "()", body[len("return ") : -1])
    return entries


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
    source = SETTINGS_LIST.read_text() + "\n" + (ROOT / "src/SettingsList.h").read_text()
    capacities = set()
    capability_names = (
        "FREEINK_CAP_FRONTLIGHT",
        "FREEINK_CAP_TOUCH",
        "CROSSPOINT_CAP_SOUND_FEEDBACK",
        "FREEINK_CAP_HAPTIC",
        "FREEINK_CAP_WARMLIGHT",
        "FREEINK_DEVICE_PICCO",
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
        assert entries.count("emit(") == entry_count, capabilities
        capacities.add(entry_count)
    # Compile the actual collector with descriptors for every capability count;
    # the list contents above come from the production visitor, including its factories.
    collector_start = source.index('std::vector<SettingInfo> getBaseSettingsList(')
    collector_open = source.index('{', collector_start)
    collector_end = matching_delimiter(source, collector_open, '{', '}')
    collector = source[collector_start:collector_end + 1]
    capacities = sorted(capacities)
    checks = r"""
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>
struct SettingInfo {size_t id;};
using SettingVisitor=void(*)(void*,SettingInfo&);
size_t descriptorCount=0;
void forEachBaseSetting(SettingVisitor visit,void* context){
  for(size_t i=0;i<descriptorCount;++i){SettingInfo info{i};visit(context,info);}
}
""" + collector + '\nint main(){for(size_t count : {' + ','.join(str(n)+'u' for n in capacities) + r"""}){
 descriptorCount=count;
 for(size_t extra:{0u,1u,4u}){
  auto rows=getBaseSettingsList(extra);
  assert(rows.size()==count && rows.capacity()>=count+extra);
  for(size_t i=0;i<count;++i)assert(rows[i].id==i);
 }
}}
"""
    import os
    import tempfile
    with tempfile.TemporaryDirectory(prefix="settings-capacity-", dir=os.environ.get("TMPDIR")) as directory:
        binary = Path(directory) / 'check'
        subprocess.run(["c++", "-std=c++20", "-x", "c++", "-", "-o", str(binary)], input=checks, text=True, check=True)
        subprocess.run([str(binary)], check=True)
    print(f"Web Settings API keys are unique across {2 ** len(capability_names)} capability combinations")
    print("Base settings capacity matches every capability combination without vector growth")
    print("Production visitor emits one descriptor at a time; collector reserves every capability count plus caller slots")


if __name__ == "__main__":
    main()
