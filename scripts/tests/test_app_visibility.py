"""Compile production visibility defaults, migration, and language transactions."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AppVisibilityTest(unittest.TestCase):
    def test_defaults_migration_and_language_selection(self):
        settings = (ROOT / 'src/CrossPointSettings.cpp').read_text()
        header = (ROOT / 'src/CrossPointSettings.h').read_text()
        selector = (ROOT / 'src/activities/settings/LanguageSelectActivity.cpp').read_text()
        profile = settings.split('constexpr CrossPointSettings::ContentProfile contentProfileForLanguage', 1)[1]
        profile = 'constexpr CrossPointSettings::ContentProfile contentProfileForLanguage' + profile.split('\n}', 1)[0] + '\n}'
        apply = settings.split('void CrossPointSettings::applyLanguageSelection', 1)[1]
        apply = 'void CrossPointSettings::applyLanguageSelection' + apply.split('\n}', 1)[0] + '\n}'
        fields = header.split('  static constexpr uint8_t APPS_CATALOG_VERSION', 1)[1]
        fields = '  static constexpr uint8_t APPS_CATALOG_VERSION' + fields.split('  uint8_t buddyClaimed', 1)[0]
        migration = settings.split('  hiddenAppsMask =', 1)[1]
        migration = '  hiddenAppsMask =' + migration.split('  buddyClaimed =', 1)[0]
        transaction = selector.split('  const uint8_t previousLanguage', 1)[1]
        transaction = '  const uint8_t previousLanguage' + transaction.split('    LOG_ERR("LANG"', 1)[0]
        source = r'''
#include "AppVisibility.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <string_view>
using namespace appVisibility;
enum class Language : uint8_t { EN, ZH_CN, FR };
struct CrossPointSettings {
    enum class ContentProfile { Global, China };
    enum { NOTOSANS = 1, CURRENT_ONBOARDING_VERSION = 1 };
    uint8_t language = 0, clockUtcOffsetQ = 48, fontFamily = 2, fontPointSize = 16, onboardingVersion = 0;
    ContentProfile contentProfile = ContentProfile::Global;
    bool saveOk = true;
    bool saveToFile() { return saveOk; }
    void applyLanguageSelection(uint8_t);
FIELDS
};
PROFILE
APPLY
struct Value {
    std::optional<uint32_t> value;
    bool isNull() const { return !value.has_value(); }
    template<class T> T as() const { return static_cast<T>(value.value()); }
    uint8_t operator|(uint8_t fallback) const { return value.value_or(fallback); }
};
struct Doc {
    Value mask, version;
    Value operator[](std::string_view key) const { return key == "hiddenAppsMask" ? mask : version; }
};
struct Settings : CrossPointSettings {
    bool needsResave = false;
    void load(Doc doc) {
MIGRATION
    }
};
Settings SETTINGS;
enum class Mode { Initial, Settings, Upgrade };
void selectLanguage(uint8_t langIndex, Mode mode_) {
TRANSACTION
        return;
    }
}
int main() {
    constexpr AppId ids[] = {AppId::ReadingStats, AppId::WeRead, AppId::Sudoku, AppId::Gomoku,
        AppId::ChineseChess, AppId::Minesweeper, AppId::Game2048, AppId::UglyAvatar,
        AppId::Standby, AppId::AirPage, AppId::Buddy, AppId::Sokoban, AppId::PixelSwitch,
        AppId::FileTransfer, AppId::OpdsBrowser, AppId::Calculator, AppId::Woodfish};
    for (unsigned i = 0; i < 17; ++i) assert(static_cast<unsigned>(ids[i]) == i);
    constexpr uint32_t expected = (1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<16);
    static_assert(DEFAULT_HIDDEN_APPS_MASK == expected);
    assert(SETTINGS.hiddenAppsMask == expected);
    SETTINGS.load({{std::nullopt}, {1}});
    assert(SETTINGS.hiddenAppsMask == expected);
    for (uint32_t mask : {0u, UINT32_MAX, expected, appBit(AppId::WeRead), 0x80000000u}) {
        SETTINGS.load({{mask}, {1}});
        assert(SETTINGS.hiddenAppsMask == mask);
        SETTINGS.load({{mask}, {std::nullopt}});
        assert(SETTINGS.hiddenAppsMask == (mask | appBit(AppId::Buddy)));
        assert(SETTINGS.needsResave && SETTINGS.appsCatalogVersion == 1);
        SETTINGS.load({{mask}, {SETTINGS.appsCatalogVersion}});
        assert(SETTINGS.hiddenAppsMask == mask);
    }
    // Exhaust all current app masks, including every visible/hidden combination.
    for (uint32_t mask = 0; mask < (1u << 17); ++mask) {
        SETTINGS.hiddenAppsMask = mask | 0x80000000u;
        for (uint8_t language : {0, 1, 2}) {
            SETTINGS.applyLanguageSelection(language);
            assert(SETTINGS.hiddenAppsMask == (mask | 0x80000000u));
            assert(SETTINGS.contentProfile == (language == 1 ? Settings::ContentProfile::China : Settings::ContentProfile::Global));
        }
    }
    for (Mode mode : {Mode::Initial, Mode::Settings, Mode::Upgrade}) {
        SETTINGS = Settings{};
        SETTINGS.hiddenAppsMask = appBit(AppId::WeRead);
        SETTINGS.saveOk = false;
        selectLanguage(1, mode);
        assert(SETTINGS.language == 0 && SETTINGS.contentProfile == Settings::ContentProfile::Global);
        assert(SETTINGS.clockUtcOffsetQ == 48 && SETTINGS.fontFamily == 2 && SETTINGS.fontPointSize == 16);
        assert(SETTINGS.onboardingVersion == 0 && SETTINGS.hiddenAppsMask == appBit(AppId::WeRead));
        SETTINGS.saveOk = true;
        selectLanguage(1, mode);
        assert(SETTINGS.language == 1 && SETTINGS.contentProfile == Settings::ContentProfile::China);
        assert(SETTINGS.hiddenAppsMask == appBit(AppId::WeRead));
    }
}
'''
        for key, value in [('FIELDS', fields), ('PROFILE', profile), ('APPLY', apply),
                           ('MIGRATION', migration), ('TRANSACTION', transaction)]:
            source = source.replace(key, value)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'visibility.cpp'
            binary = Path(directory) / 'visibility'
            cpp.write_text(source)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
