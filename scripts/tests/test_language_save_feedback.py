"""Compile the actual language-selection transaction with a failing settings store."""
import unittest

from test_reading_ui_regressions import ROOT, method, run_cpp


class LanguageSaveFeedbackTest(unittest.TestCase):
    def test_failure_keeps_saved_marker_and_retry_completes(self):
        source = (ROOT / "src/activities/settings/LanguageSelectActivity.cpp").read_text()
        program = r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#define LOG_ERR(...) ((void)0)
enum class Language : uint8_t { EN, ZH_CN, FR };
constexpr uint8_t SORTED_LANGUAGE_INDICES[] = {1, 0, 2};
enum { STR_SELECTED };
const char* tr(int) { return "Selected"; }
struct CrossPointSettings {
  enum class ContentProfile { Global, China };
  enum { NOTOSANS=1, CURRENT_ONBOARDING_VERSION=1 };
  uint8_t language=0, clockUtcOffsetQ=44, fontFamily=7, fontPointSize=18, onboardingVersion=0;
  ContentProfile contentProfile=ContentProfile::Global;
  bool saveOk=false;
  void applyLanguageSelection(uint8_t value) {
    language=value; contentProfile=value==1 ? ContentProfile::China : ContentProfile::Global;
  }
  bool saveToFile() { return saveOk; }
} SETTINGS;
struct {
  Language language=Language::EN;
  int changes=0;
  void setLanguage(Language value) { language=value; ++changes; }
} I18N;
namespace timezones { int changes=0; void applyToClock() { ++changes; } }
struct RenderLock { template<class T> explicit RenderLock(T&) {} };
struct LanguageSelectActivity {
  enum class Mode { Settings, Initial, Upgrade };
  Mode mode_;
  bool saveFailed_=false;
  static constexpr int totalItems=3;
  struct { const char* value=nullptr; } rowItems[totalItems];
  struct { int selected=0; } nav;
  struct { void clearTapFlash() {} } app;
  int updates=0, finishes=0, homes=0;
  explicit LanguageSelectActivity(Mode mode): mode_(mode) { rowItems[1].value=tr(STR_SELECTED); }
  bool isOnboarding() const { return mode_!=Mode::Settings; }
  void requestUpdate() { ++updates; }
  void finish() { ++finishes; }
  void onGoHome() { ++homes; }
  void activateIndex(int);
};
'''
        program += method(source, "void LanguageSelectActivity::activateIndex")
        program += r'''
int main() {
  for (auto mode : {LanguageSelectActivity::Mode::Settings, LanguageSelectActivity::Mode::Initial,
                    LanguageSelectActivity::Mode::Upgrade}) {
    SETTINGS=CrossPointSettings{};
    I18N.language=Language::EN; I18N.changes=0; timezones::changes=0;
    LanguageSelectActivity activity(mode);
    for (int row : {0,2}) {
      activity.activateIndex(row);
      assert(activity.saveFailed_ && activity.homes==0 && activity.finishes==0);
      assert(SETTINGS.language==0 && SETTINGS.contentProfile==CrossPointSettings::ContentProfile::Global);
      assert(SETTINGS.clockUtcOffsetQ==44 && SETTINGS.fontFamily==7 && SETTINGS.fontPointSize==18);
      assert(SETTINGS.onboardingVersion==0 && I18N.changes==0 && timezones::changes==0);
      assert(activity.rowItems[1].value==tr(STR_SELECTED));
      assert(activity.rowItems[0].value==nullptr && activity.rowItems[2].value==nullptr);
    }
    assert(activity.updates==2);
    SETTINGS.saveOk=true;
    activity.activateIndex(0);
    assert(!activity.saveFailed_ && SETTINGS.language==1);
    assert(SETTINGS.contentProfile==CrossPointSettings::ContentProfile::China);
    assert(I18N.language==Language::ZH_CN && I18N.changes==1 && timezones::changes==1);
    assert(activity.homes==(mode!=LanguageSelectActivity::Mode::Settings));
    assert(activity.finishes==(mode==LanguageSelectActivity::Mode::Settings));
    if (mode==LanguageSelectActivity::Mode::Initial) {
      assert(SETTINGS.clockUtcOffsetQ==80 && SETTINGS.fontFamily==CrossPointSettings::NOTOSANS);
      assert(SETTINGS.fontPointSize==12 && SETTINGS.onboardingVersion==1);
    } else {
      assert(SETTINGS.clockUtcOffsetQ==44 && SETTINGS.fontFamily==7 && SETTINGS.fontPointSize==18);
      assert(SETTINGS.onboardingVersion==(mode==LanguageSelectActivity::Mode::Upgrade));
    }
  }
}
'''
        run_cpp(program, defines=("SIMULATOR",))


if __name__ == "__main__":
    unittest.main()
