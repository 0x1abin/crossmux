#!/usr/bin/env python3
"""Compare real theme bridges and SDK draw/hit traces with reviewed upstream.

Uses production candidate metrics and font IDs against the pinned upstream.
This checks shared drawing/hit geometry; whole screens are tested in the simulator.
Also compares the working INX bridge with HEAD, without changing its baseline.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def source(ref, path):
    if ref is None:
        return (ROOT / path).read_text()
    return subprocess.check_output(['git', '-C', str(ROOT), 'show', f'{ref}:{path}'], text=True)


def metric_namespace(ref, path, name):
    text = source(ref, path)
    return re.search(rf'namespace {name} \{{.*?\n\}}[^\n]*\n+(?=class )', text, re.S).group()


def trace(sdk, reader_ref, metrics_ref, directory, inx=False):
    directory.mkdir()
    for name in ('UIThemeTokens.h', 'UIScale.h'):
        path = 'src/components/' + name
        text = source(reader_ref, path) if reader_ref else (ROOT / path).read_text()
        (directory / name).write_text(text)
    (directory / 'fontIds.h').write_text(source(metrics_ref, 'src/fontIds.h'))
    settings = '''#pragma once
struct CrossPointSettings {
  enum class UI_THEME { CLASSIC, INX };
  UI_THEME uiTheme = UI_THEME::THEME;
};
inline CrossPointSettings paritySettings;
#define SETTINGS paritySettings
'''.replace('UI_THEME::THEME', 'UI_THEME::INX' if inx else 'UI_THEME::CLASSIC')
    (directory / 'CrossPointSettings.h').write_text(settings)
    (directory / 'BoardConfig.h').write_text('''#pragma once
namespace BoardConfig {
inline bool touch = false;
inline bool hasTouch() { return touch; }
struct Profile { struct { int left, right; } viewableInsets; };
inline constexpr Profile ACTIVE{{7, 9}};
}
''')
    (directory / 'FreeInkUIGfxRenderer.h').write_text('''#pragma once
namespace freeink { namespace ui { using GfxRendererTarget = ::TraceTarget; } }
''')
    (directory / 'SelectionCursorPolicy.h').write_text('''#pragma once
namespace SelectionCursorPolicy {
inline void hideFreeInkListFocus(freeink::ui::ThemeTokens&) {}
}
''')
    base = source(metrics_ref, 'src/components/themes/BaseTheme.h')
    metrics_type = re.search(r'struct ThemeMetrics \{.*?\n\};', base, re.S).group()
    # The fork adds opt-in INX fields that do not exist in upstream metrics.
    for field in ('int listSeparatorStyle;', 'int listValueMaxWidth;', 'bool listSelectionCoversScrollReservation;'):
        if field.split()[-1].rstrip(';') not in metrics_type:
            metrics_type = metrics_type.replace('\n};', f'\n  {field}\n}};')
    metrics = [metric_namespace(metrics_ref, 'src/components/themes/lyra/LyraTheme.h', 'LyraMetrics')]
    if inx:
        metrics.append(metric_namespace(metrics_ref, 'src/components/themes/inx/InxTheme.h', 'InxMetrics'))
        names = ['InxMetrics']
    else:
        metrics += [metric_namespace(metrics_ref, 'src/components/themes/BaseTheme.h', 'BaseMetrics'),
                    metric_namespace(metrics_ref, 'src/components/themes/roundedraff/RoundedRaffTheme.h', 'RoundedRaffMetrics'),
                    metric_namespace(metrics_ref, 'src/components/themes/lyra/Lyra3CoversTheme.h', 'Lyra3CoversMetrics')]
        # Cover Grid uses Lyra metrics outside its dedicated home renderer.
        names = ['BaseMetrics', 'LyraMetrics', 'RoundedRaffMetrics', 'Lyra3CoversMetrics', 'LyraMetrics']
    (directory / 'UITheme.h').write_text('#pragma once\n#include "CrossPointSettings.h"\n' + metrics_type + '\n' +
        '\n'.join(metrics) + '\ninline constexpr ThemeMetrics parityMetrics[] = {' +
        ', '.join(name + '::values' for name in names) + '''};
class UITheme {
 public:
  const ThemeMetrics* metrics = &parityMetrics[0];
  static UITheme& getInstance() { static UITheme theme; return theme; }
  const ThemeMetrics& getMetrics() const { return *metrics; }
  bool showSelectionCursor() const { return true; }
};
''')
    ui = sdk / 'libs/ui/FreeInkUI'
    binary = directory / 'trace'
    subprocess.run(['c++', '-std=c++20', '-DUPSTREAM_THEME_PARITY', '-I' + str(directory),
                    '-I' + str(ui / 'include'), str(ui / 'src/FreeInkUI.cpp'),
                    str(HERE / 'InxStyleParity.cpp'), '-o', str(binary)], check=True)
    output = subprocess.check_output([str(binary)], text=True)
    ids = (directory / 'fontIds.h').read_text()
    for name, number in re.findall(r'#define (UI_10_FONT_ID|UI_12_FONT_ID|SMALL_FONT_ID) \((-?\d+)\)', ids):
        output = re.sub(r'(?<![0-9])' + number + r'(?![0-9])', name, output)
    return output


def compare(actual, expected, label):
    if actual != expected:
        actual_lines, expected_lines = actual.splitlines(), expected.splitlines()
        for index, (a, e) in enumerate(zip(actual_lines, expected_lines)):
            if a != e:
                raise AssertionError(f'{label}, line {index + 1}:\n  candidate: {a}\n  reference: {e}')
        raise AssertionError(f'{label}: different trace lengths')
    print(f'{label}: {actual.count("SCENE ")} drawing/hit traces match')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=ROOT / 'freeink-sdk')
    parser.add_argument('--upstream-sdk', type=Path, required=True)
    parser.add_argument('--upstream-reader-ref', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='theme-parity-', dir=os.environ.get('TMPDIR')) as tmp:
        tmp = Path(tmp)
        compare(trace(args.sdk, None, None, tmp / 'candidate'),
                trace(args.upstream_sdk, args.upstream_reader_ref, args.upstream_reader_ref, tmp / 'upstream'),
                'Non-INX production metrics and SDK defaults')
        compare(trace(args.sdk, None, None, tmp / 'inx-current', inx=True),
                trace(args.sdk, 'HEAD', 'HEAD', tmp / 'inx-before', inx=True),
                'INX bridge against pre-sync HEAD')


if __name__ == '__main__':
    main()
