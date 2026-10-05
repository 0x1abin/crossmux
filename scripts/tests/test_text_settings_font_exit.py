"""Compile the production exit and .cpfont unload paths, including repeated visits."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class TextSettingsFontExitTest(unittest.TestCase):
    def test_exit_unloads_cpfont_before_base_exit_and_preserves_vector_faces(self):
        exit_source = (ROOT / 'src/activities/settings/TextSettingsActivity.cpp').read_text()
        system = (ROOT / 'src/SdCardFontSystem.cpp').read_text()
        manager = (ROOT / 'lib/EpdFont/SdCardFontManager.cpp').read_text()
        run_cpp(r'''
#include <cassert>
#include <string>
#include <vector>
struct Font { static inline int live=0; Font(){++live;} ~Font(){--live;} };
struct GfxRenderer {
 bool framebuffer=true, preferred=true, sd=true, builtin=true, vector=true;
 bool hasFrameBuffer() const {return framebuffer;}
 void clearPreferredFonts(){preferred=false;}
 void clearSdCardFonts(){sd=false;}
 void removeFont(int id){assert(id==42);}
};
struct SdCardFontManager {
 struct Loaded {int fontId; Font* font;};
 std::vector<Loaded> loaded_;
 std::string loadedFamilyName_;
 unsigned loadedPointSize_=0;
 void unloadAll(GfxRenderer&);
};
struct SdCardFontSystem {SdCardFontManager manager_; void releaseLoadedFont(GfxRenderer&);};
SdCardFontSystem sdFontSystem;
struct Activity {
 bool exited=false;
 void onExit(){assert(Font::live==0); exited=true;}
};
struct TextSettingsActivity: Activity {GfxRenderer renderer; void onExit();};
''' + method(manager, 'void SdCardFontManager::unloadAll(')
            + method(system, 'void SdCardFontSystem::releaseLoadedFont(')
            + method(exit_source, 'void TextSettingsActivity::onExit()') + r'''
int main(){
 TextSettingsActivity activity;
 for(int visit=0;visit<4;++visit){
  // The next reader/settings load can register the same canonical family again.
  sdFontSystem.manager_.loaded_.push_back({42,new Font});
  sdFontSystem.manager_.loadedFamilyName_="Reader";
  sdFontSystem.manager_.loadedPointSize_=22+visit;
  activity.renderer.preferred=activity.renderer.sd=true;
  activity.onExit();
  assert(activity.exited && Font::live==0);
  assert(sdFontSystem.manager_.loaded_.empty());
  assert(sdFontSystem.manager_.loadedFamilyName_.empty());
  assert(sdFontSystem.manager_.loadedPointSize_==0);
  assert(!activity.renderer.preferred && !activity.renderer.sd);
  assert(activity.renderer.builtin && activity.renderer.vector);
 }
 activity.renderer.framebuffer=false;
 activity.renderer.preferred=activity.renderer.sd=true;
 activity.onExit();
 assert(activity.renderer.preferred && activity.renderer.sd);
}
''')

    def test_parent_settings_and_reader_reload_the_selected_font_after_exit(self):
        reader = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        settings = (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text()
        menu = reader[reader.index('case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS:'):]
        setting = settings[settings.index('case SettingAction::TextSettings:'):]
        reader_callback = method(menu, '[this](const ActivityResult&)')
        settings_callback = method(setting, '[this](const ActivityResult&)')
        run_cpp(r'''
#include <cassert>
#include <memory>
static bool locked=false;
struct RenderLock {
 RenderLock(){assert(!locked);locked=true;}
 template<typename T> explicit RenderLock(T&):RenderLock(){}
 ~RenderLock(){locked=false;}
};
struct ActivityResult{};
struct {void saveToFile(){}} SETTINGS;
struct {void resumeSession(){}} READING_STATS;
struct {bool loaded=false;void ensureLoaded(int){assert(locked);loaded=true;}} sdFontSystem;
struct Section {int pageCount=8,currentPage=3;};
struct EpubReaderActivity {
 int renderer=0,cachedSpineIndex=0,currentSpineIndex=2,cachedChapterTotalPageCount=0,nextPageNumber=0;
 std::unique_ptr<Section> section=std::make_unique<Section>();
 void rememberCurrentContentOffset(){}
 void openReaderMenu(){assert(sdFontSystem.loaded&&!locked);}
 void applyReaderTextSettings();
 void returned(){auto callback=''' + reader_callback + r''';callback(ActivityResult{});}
};
struct SettingsActivity {
 int renderer=0;
 void rebuildSettingsLists(){assert(sdFontSystem.loaded&&!locked);}
 void requestUpdate(){assert(sdFontSystem.loaded&&!locked);}
 void returned(){auto callback=''' + settings_callback + r''';callback(ActivityResult{});}
};
''' + method(reader, 'void EpubReaderActivity::applyReaderTextSettings(') + r'''
int main(){
 EpubReaderActivity reader;reader.returned();
 assert(sdFontSystem.loaded&&!reader.section&&reader.nextPageNumber==3&&!locked);
 sdFontSystem.loaded=false;SettingsActivity settings;settings.returned();
 assert(sdFontSystem.loaded&&!locked);
}
''')


if __name__ == '__main__':
    unittest.main()
