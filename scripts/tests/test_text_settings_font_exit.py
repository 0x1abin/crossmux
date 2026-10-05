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


if __name__ == '__main__':
    unittest.main()
