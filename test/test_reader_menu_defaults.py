#!/usr/bin/env python3
"""Compile production reader-menu defaults and enum persistence for each board."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    header = (ROOT / "src/CrossPointSettings.h").read_text()
    source = (ROOT / "src/CrossPointSettings.cpp").read_text()
    menu_enum = re.search(r"  enum READER_MENU_STYLE \{[^}]+\};", header).group()
    start = header.index("\n", header.index("  uint8_t focusReadingEnabled ="))
    defaults = header[start:header.index("  // Image resampling", start)]
    start = source.index("      const uint8_t fieldDefault =", source.index("bool CrossPointSettings::fromJson("))
    end = source.index("      s.*(info.valuePtr) = v;", start) + len("      s.*(info.valuePtr) = v;")
    loader = source[start:end]
    clamp = re.search(r"  auto clamp = .*?; \};", source, re.S).group()
    saver = re.search(r"      doc\[info.key\] = s\.\*\(info.valuePtr\);", source).group()
    program = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string_view>
struct CrossPointSettings {
@ENUM@
@DEFAULTS@
  uint8_t uiTheme = 0;
};
enum class SettingType { ENUM, TOGGLE, VALUE };
constexpr uint8_t COVER_GRID = 9;
struct SettingInfo {
  const char* key = "readerMenuStyle";
  uint8_t CrossPointSettings::* valuePtr = &CrossPointSettings::readerMenuStyle;
  SettingType type = SettingType::ENUM;
  uint8_t enumValueCount = 0;
  std::array<uint8_t, CrossPointSettings::READER_MENU_STYLE_COUNT> enumLabels() const { return {}; }
  struct { uint8_t min = 0, max = 255; } valueRange;
};
struct JsonDocument {
  std::optional<uint8_t> value;
  auto& operator[](const char* key) const {
    assert(std::string_view(key) == "readerMenuStyle");
    return value;
  }
  auto& operator[](const char* key) {
    assert(std::string_view(key) == "readerMenuStyle");
    return value;
  }
};
uint8_t operator|(const std::optional<uint8_t>& value, uint8_t fallback) { return value.value_or(fallback); }
CrossPointSettings load(const JsonDocument& doc) {
  CrossPointSettings s;
  const SettingInfo info;
@CLAMP@
@LOADER@
  return s;
}
JsonDocument save(const CrossPointSettings& s) {
  JsonDocument doc;
  const SettingInfo info;
@SAVER@
  return doc;
}
int main() {
  const CrossPointSettings fresh;
  assert(fresh.readerMenuStyle == EXPECTED_STYLE);
  assert(load({}).readerMenuStyle == EXPECTED_STYLE);
  assert(load({CrossPointSettings::READER_MENU_STYLE_COUNT}).readerMenuStyle == EXPECTED_STYLE);
  assert(load({255}).readerMenuStyle == EXPECTED_STYLE);
  for (uint8_t style : {CrossPointSettings::READER_MENU_LIST, CrossPointSettings::READER_MENU_TOOLBAR}) {
    const auto loaded = load({style});
    assert(loaded.readerMenuStyle == style);
    assert(save(loaded).value == style);
    assert(load(save(loaded)).readerMenuStyle == style);
  }
  assert(load(save(load({}))).readerMenuStyle == EXPECTED_STYLE);
}
'''
    for token, snippet in (("ENUM", menu_enum), ("DEFAULTS", defaults), ("CLAMP", clamp),
                           ("LOADER", loader), ("SAVER", saver)):
        program = program.replace(f"@{token}@", snippet)
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    with tempfile.TemporaryDirectory(prefix="reader-menu-defaults-") as directory:
        binary = Path(directory) / "check"
        for name, defines, expected in (
            ("other", (), 0),
            ("readpico", ("FREEINK_DEVICE_READPICO=1",), 1),
            ("metalio_plus", ("FREEINK_DEVICE_METALIO_EINK4_PLUS=1",), 1),
        ):
            subprocess.run(compiler + ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-x", "c++", "-",
                                      f"-DEXPECTED_STYLE={expected}"] + [f"-D{flag}" for flag in defines]
                           + ["-o", str(binary)], input=program, text=True, check=True)
            subprocess.run([str(binary)], check=True)
            print(f"{name}: defaults, missing/invalid keys and saved choices passed", flush=True)


if __name__ == "__main__":
    main()
