"""Compile production glyph decoding and reader native-gray failure branches."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class VectorGrayFallbackTest(unittest.TestCase):
    def test_four_bit_glyphs_in_native_bw_selector_rotated_and_scaled_paths(self):
        source = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        header = (ROOT / 'lib/GfxRenderer/GfxRenderer.h').read_text()
        helpers = ''.join(method(source, name) for name in (
            'static void drawGlyphPixel(', 'constexpr uint8_t dilate2BitCoverage(',
            'static uint8_t get4BitCoverage(', 'static uint8_t get2BitCoverage(',
            'static void draw2BitGlyphPixel(', 'static void draw4BitGlyphPixel('))
        run_cpp(r'''
#include <array>
#include <cassert>
#include <cstdint>
struct EpdGlyph {uint8_t width=16,height=1; int left=0,top=0;};
struct EpdFontData {bool is2Bit=true,is4Bit=true; int ascender=0;};
struct EpdFontFamily {
 enum Style {REGULAR}; EpdGlyph glyph; EpdFontData data;
 const EpdGlyph* getGlyph(uint32_t,Style)const{return &glyph;}
 const EpdFontData* getData(Style)const{return &data;}
};
struct GfxRenderer {
 enum RenderMode {BW,GRAYSCALE_LSB,GRAYSCALE_MSB};
 struct TwoBitPixel {bool draw,state;};
''' + method(header, 'static constexpr TwoBitPixel mapTwoBitPixel(')
            + method(header, 'static constexpr TwoBitPixel mapTwoBitGlyphCoverage(') + r'''
 bool native=false;
 std::array<uint8_t,8> packed{0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
 mutable std::array<int,32> pixels,grays;
 GfxRenderer(){pixels.fill(-1);grays.fill(-1);}
 bool grayPlanesAreAbsolute()const{return false;}
 bool glyphIntersectsStrip(int,int,int,int)const{return true;}
 bool isGrayscale16Active()const{return native;}
 const uint8_t* getGlyphBitmap(const EpdFontData*,const EpdGlyph*)const{return packed.data();}
 void drawPixel(int x,int y,bool state)const{assert(x==0||y==0);pixels[x+y]=state;}
 void drawGrayscale16Pixel(int x,int y,uint8_t gray)const{grays[x+y]=gray;}
};
enum class TextRotation {None,Rotated90CW};
template<TextRotation R=TextRotation::None,typename... Args>
void renderMissingGlyph(Args...){assert(false);}
''' + helpers + '\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wunused-but-set-parameter\"\n'
            + method(source, 'static void renderCharScaled(') + '\n#pragma GCC diagnostic pop\n'
            + '\ntemplate<TextRotation rotation=TextRotation::None>\n'
            + method(source, 'static void renderCharImpl(') + r'''
int main(){
 EpdFontFamily font;
 for(auto mode:{GfxRenderer::BW,GfxRenderer::GRAYSCALE_LSB,GfxRenderer::GRAYSCALE_MSB}){
  GfxRenderer r; renderCharImpl(r,mode,font,65,0,0,true,EpdFontFamily::REGULAR);
  for(int i=0;i<16;++i){auto p=GfxRenderer::mapTwoBitGlyphCoverage(mode,i>>2);
   assert(r.pixels[i]==(p.draw?int(mode==GfxRenderer::BW?true:p.state):-1));}
  GfxRenderer rotated;
  renderCharImpl<TextRotation::Rotated90CW>(rotated,mode,font,65,0,15,true,EpdFontFamily::REGULAR);
  for(int i=0;i<16;++i)assert(rotated.pixels[15-i]==r.pixels[i]);
 }
 GfxRenderer native; native.native=true;
 renderCharImpl(native,GfxRenderer::BW,font,65,0,0,true,EpdFontFamily::REGULAR);
 assert(native.grays[0]==-1);
 for(int i=1;i<16;++i)assert(native.grays[i]==(15-i)*17);
 for(bool ink:{false,true}) for(uint8_t bold:{uint8_t(0),uint8_t(1),uint8_t(3)}){
  if(ink&&bold==0)continue;
  GfxRenderer a,b; a.native=true;
  renderCharImpl(a,GfxRenderer::BW,font,65,0,0,ink,EpdFontFamily::REGULAR,bold);
  renderCharImpl(b,GfxRenderer::BW,font,65,0,0,ink,EpdFontFamily::REGULAR,bold);
  assert(a.pixels==b.pixels);
 }
 // Original reported sample must be 1010 in BW, with no 2bpp reinterpretation.
 GfxRenderer sample;sample.packed={0xf0,0xf0};font.glyph.width=4;
 renderCharImpl(sample,GfxRenderer::BW,font,65,0,0,true,EpdFontFamily::REGULAR);
 assert(sample.pixels[0]==1&&sample.pixels[1]==-1&&sample.pixels[2]==1&&sample.pixels[3]==-1);
 GfxRenderer scaled;scaled.packed={0xf0,0xf0};
 renderCharScaled(scaled,GfxRenderer::BW,font,65,0,0,true,EpdFontFamily::REGULAR,0);
 assert(scaled.pixels[0]==1&&scaled.pixels[1]==1);
}
''')

    def test_four_bit_packing_preserves_all_legacy_two_bit_thresholds(self):
        ttf = (ROOT / 'lib/EpdFont/TtfEpdFont.cpp').read_text()
        renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        packing = method(ttf, 'if (f.fourBit)')
        helpers = ''.join(method(renderer, name) for name in (
            'static uint8_t get4BitCoverage(', 'static uint8_t get2BitCoverage('))
        run_cpp(r'''
#include <cassert>
#include <cstdint>
''' + helpers + r'''
int main(){
 uint8_t dst[128]{};
 struct {bool fourBit=true;} f;
 for(int i=0;i<256;++i){
  const uint8_t a=static_cast<uint8_t>(i);
''' + packing + r'''
  const uint8_t legacy=a<64?0:a<128?1:a<192?2:3;
  assert(get2BitCoverage(dst,i,true)==legacy);
 }
}
''')

    def test_reader_start_commit_failure_submits_complete_bw_and_gates_images_background_bold_aa(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        a = source.index('  const bool use16LevelText =')
        eligibility = source[a:source.index(';', a)+1]
        a = source.index('  if (use16LevelText) {', source.index('  const auto tDisplay'))
        branch = method(source[a:], 'if (use16LevelText)')
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <utility>
template<typename... T> void log(T&&...){}
#define LOG_DBG(...) log(__VA_ARGS__)
#define LOG_ERR(...) log(__VA_ARGS__)
unsigned millis(){return 0;}
struct HalDisplay {enum Mode {FULL_REFRESH};};
struct GfxRenderer {
 enum Mode {BW}; bool start=true,commit=true,active=false,page=false,status=false;
 int bwFrames=0,nativeFrames=0; unsigned levels=16;
 unsigned getGrayscaleLevels(){return levels;}
 bool beginGrayscale16(){if(start){active=true;page=status=false;}return start;}
 bool commitGrayscale16(){++nativeFrames;active=false;return commit;}
 void cancelGrayscale16(){active=false;}
 void setRenderMode(Mode){assert(!active);}
 void clearScreen(){page=status=false;}
 void displayBuffer(HalDisplay::Mode){assert(!active&&page&&status);++bwFrames;}
};
struct {bool readingBackgroundEnabled=false;int fakeBold=0;} SETTINGS;
struct {bool four=true;bool readerFaceIsFourBit(){return four;}} sdFontSystem;
struct {bool switching=false;bool isSwitchPending(){return switching;}} activityManager;
struct Probe {
 GfxRenderer renderer;bool needsTextGrayscale=true,pageHasImages=false;int pagesUntilFullRefresh=5;
 bool eligible(){''' + eligibility + r''' return use16LevelText;}
 void render(){const bool use16LevelText=eligible();const unsigned t0=0;
  const auto renderPageWithGuideLines=[&]{renderer.page=true;};
  const auto renderStatusBar=[&]{renderer.status=true;};
''' + branch + r'''
 }
};
int main(){
 for(bool start:{false,true})for(bool commit:{false,true}){
  Probe p;p.renderer.start=start;p.renderer.commit=commit;p.render();
  assert(!p.renderer.active&&p.pagesUntilFullRefresh==1);
  assert(p.renderer.bwFrames==int(!start||!commit));
  assert(p.renderer.nativeFrames==int(start));
 }
 for(bool images:{false,true})for(bool background:{false,true})
 for(bool aa:{false,true})for(int bold:{0,1,2,3}){
  SETTINGS.readingBackgroundEnabled=background;SETTINGS.fakeBold=bold;
  Probe p;p.pageHasImages=images;p.needsTextGrayscale=aa;
  assert(p.eligible()==(!images&&!background&&aa&&bold==0));
 }
 SETTINGS.readingBackgroundEnabled=false;SETTINGS.fakeBold=0;
 Probe p;p.needsTextGrayscale=false;assert(!p.eligible());
 p.needsTextGrayscale=true;p.renderer.levels=4;assert(!p.eligible());
 p.renderer.levels=16;sdFontSystem.four=false;assert(!p.eligible());
 sdFontSystem.four=true;activityManager.switching=true;p.render();
 assert(!p.renderer.active&&p.renderer.bwFrames==0&&p.renderer.nativeFrames==0);
}
''')


if __name__ == '__main__':
    unittest.main()
