"""Execute both production preload callbacks: Flash may resume only after refresh."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class FontPreloadRefreshTest(unittest.TestCase):
    def test_callbacks_wait_outside_lock_and_remain_throttled(self):
        for path, activity in (
            ('settings/TextSettingsActivity.cpp', 'TextSettingsActivity'),
            ('boot_sleep/BootActivity.cpp', 'BootActivity'),
        ):
            with self.subTest(activity=activity):
                source = (ROOT / 'src/activities' / path).read_text()
                callback = method(source, '[](size_t completed, size_t total, void* context)')
                run_cpp(r'''
#include <cassert>
#include <cstddef>
#include <atomic>
static bool locked = false;
enum class Stage { Copying, Verifying };
struct Activity {
 std::atomic<size_t> preloadTotal_{1}, preloadCompleted_{0}, total_{1}, completed_{0};
 std::atomic<Stage> stage_{Stage::Copying};
 bool preloadVerifying_ = false;
 unsigned lastPreloadPercent_ = 0, lastRequestedPercent_ = 0;
 int framesCompleted = 0;
 void requestUpdateAndWait() { assert(!locked); ++framesCompleted; }
 void requestUpdate(bool) { assert(false && "asynchronous refresh overlaps next Flash write"); }
};
using TextSettingsActivity = Activity;
using BootActivity = Activity;
struct RenderLock {
 explicit RenderLock(Activity&) { assert(!locked); locked = true; }
 ~RenderLock() { locked = false; }
};
int main() {
 Activity activity;
 auto progress = ''' + callback + r''';
 progress(0, 0, &activity); assert(activity.framesCompleted == 0);
 progress(1, 1000, &activity); assert(activity.framesCompleted == 0);
 progress(100, 1000, &activity); assert(activity.framesCompleted == 1);
 progress(199, 1000, &activity); assert(activity.framesCompleted == 1);
 progress(500, 1000, &activity); assert(activity.framesCompleted == 2);
 progress(501, 1000, &activity); assert(activity.framesCompleted == 3);
 progress(599, 1000, &activity); assert(activity.framesCompleted == 3);
 progress(600, 1000, &activity); assert(activity.framesCompleted == 4);
 progress(1000, 1000, &activity); assert(activity.framesCompleted == 5);
 assert(!locked);
}
''')

    def test_percentage_text_boundaries(self):
        source = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
        draw = method(source, 'int BaseTheme::drawProgressBar(')
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
struct Rect { int x, y, width, height; };
constexpr int UI_10_FONT_ID = 0;
struct GfxRenderer {
 mutable std::string text;
 void drawRect(int,int,int,int) const {}
 void fillRect(int,int,int,int) const {}
 void drawCenteredText(int,int,const char* value) const { text = value; }
};
struct BaseTheme {
 int measureProgressBarHeight(const GfxRenderer&,int,bool) const { return 1; }
 int drawProgressBar(const GfxRenderer&,Rect,size_t,size_t,bool) const;
};
''' + draw + r'''
int main() {
 BaseTheme theme; GfxRenderer renderer; Rect rect{0,0,400,20};
 theme.drawProgressBar(renderer,rect,0,1,true); assert(renderer.text == "0%");
 theme.drawProgressBar(renderer,rect,6549504,13099008,true); assert(renderer.text == "50%");
 theme.drawProgressBar(renderer,rect,6549505,13099008,true); assert(renderer.text == "50%");
 theme.drawProgressBar(renderer,rect,13099008,13099008,true); assert(renderer.text == "100%");
 renderer.text.clear(); theme.drawProgressBar(renderer,rect,0,0,true); assert(renderer.text.empty());
}
''')


if __name__ == '__main__':
    unittest.main()
