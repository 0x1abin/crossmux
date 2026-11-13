"""Exercise production chrome drawing and maintained-SVG generation."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_reading_ui_regressions import ROOT, method, run_cpp
from test_builtin_font_shrink import rows

HEADER = ROOT / 'src/components/icons/uiChromeIcons.h'


def arrays(text):
    return {name: bytes(int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]{2})', body))
            for name, body in re.findall(r'uint8_t\s+(\w+)\[\]\s*=\s*\{([^}]+)\}', text)}


class UiChromeIconTest(unittest.TestCase):
    def test_battery_metrics_scale_once_and_include_the_terminal(self):
        source = (ROOT / 'src/components/themes/BaseTheme.h').read_text()
        program = '#include <cassert>\n#include <initializer_list>\n'
        program += method(source, 'struct ThemeMetrics {') + ';\n'
        program += 'namespace UiHighDpiProfile {\n' + method(source, 'constexpr void apply(') + '\n}\n'
        program += r'''
int main() {
  for(int width : {15,16}) {
    ThemeMetrics metrics{}; metrics.batteryWidth=width; metrics.batteryHeight=12;
    for(int repeat=0;repeat<3;++repeat) {
      UiHighDpiProfile::apply(metrics);
      assert(metrics.batteryWidth==(UiHighDpiProfile::enabled?(width+1)*2:width));
      assert(metrics.batteryHeight==(UiHighDpiProfile::enabled?24:12));
    }
  }
}
'''
        for defines in ((), ('CROSSMUX_UI_PROFILE_HIGH_DPI',)):
            run_cpp(program, defines=defines)

    def test_native_dimensions_and_white_padding(self):
        data = arrays(HEADER.read_text())
        self.assertEqual(len(data), 9)
        for name, bits in data.items():
            size = 56 if name.endswith('_56_bits') else 48 if name.endswith('_48_bits') else 32 if name.endswith('_32_bits') else 24 if name.endswith('_24_bits') else None
            self.assertEqual(len(bits), size * ((size + 7) // 8) if size else 80)
            if name.endswith('_56_bits'):
                self.assertEqual(bits[:7], b'\xff' * 7)
                self.assertEqual(bits[-7:], b'\xff' * 7)
            self.assertTrue(any(byte != 255 for byte in bits))

    @unittest.skipUnless(shutil.which('rsvg-convert'), 'optional SVG generation requires rsvg-convert')
    def test_regeneration_matches_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'chrome.h'
            subprocess.run([sys.executable, str(ROOT / 'scripts/build_ui_chrome_icons.py'),
                            '--out', str(output)], check=True)
            self.assertEqual(arrays(output.read_text()), arrays(HEADER.read_text()))
            first = output.read_bytes()
            subprocess.run([sys.executable, str(ROOT / 'scripts/build_ui_chrome_icons.py'),
                            '--out', str(output)], check=True)
            self.assertEqual(first, output.read_bytes())
            for manifest, header in (('listIcons', 'listIcons48'), ('readerToolbarIcons', 'readerToolbarIcons48')):
                command = [sys.executable, str(ROOT / 'freeink-sdk/libs/assets/Icons/tools/gen_icons.py'),
                           '--manifest', str(ROOT / f'src/components/icons/{manifest}.manifest'),
                           '--svgdir', str(ROOT / 'src/components/icons/sources/lucide'),
                           '--sizes', '48', '--out', str(output)]
                subprocess.run(command, check=True)
                self.assertEqual(arrays(output.read_text()),
                                 arrays((ROOT / f'src/components/icons/{header}.h').read_text()))
                first = output.read_bytes()
                subprocess.run(command, check=True)
                self.assertEqual(first, output.read_bytes())

    def test_tab_pixels_use_target_bitmap_without_resampling(self):
        source = (ROOT / 'src/components/themes/inx/InxTheme.cpp').read_text()
        run_cpp(r'''
#include <cassert>
#include <initializer_list>
#include "Icon.h"
#include "components/icons/uiChromeIcons.h"
struct GfxRenderer {
 mutable bool pixels[56][56]{};
 void drawPixel(int x,int y,bool ink) const { assert(x>=0 && x<56 && y>=0 && y<56); pixels[y][x]=ink; }
};
constexpr int kIconSize=56;
''' + method(source, 'void drawInxIcon(') + r'''
int main() {
 for (auto bits : {icon_tab_recent_56.bits,icon_tab_library_56.bits,
                   icon_tab_settings_56.bits,icon_tab_statistics_56.bits,
                   icon_tab_apps_56.bits}) {
   GfxRenderer r; drawInxIcon(r,bits,0,0);
   for (int y=0;y<56;++y) for(int x=0;x<56;++x)
     assert(r.pixels[y][x]==((bits[y*7+x/8]&(0x80U>>(x%8)))==0));
 }
}
''', include_dirs=(ROOT / 'src', ROOT / 'freeink-sdk/libs/assets/Icons/include'),
                defines=('CROSSMUX_UI_PROFILE_HIGH_DPI',))

    def test_battery_is_double_the_legacy_pixels_in_headers_and_readers(self):
        source = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
        lyra = (ROOT / 'src/components/themes/lyra/LyraTheme.cpp').read_text()
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "components/BatteryIcon.h"
struct Rect { int x,y,width,height; };
struct { bool charging=false; bool isUsbConnected() const { return charging; } } gpio;
struct GfxRenderer {
  mutable bool pixels[40][40]{};
  void drawPixel(int x,int y,bool black=true) const {
    assert(x>=0 && x<40 && y>=0 && y<40); pixels[y][x]=black;
  }
  void fillRect(int x,int y,int w,int h,bool black=true) const {
    for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col) drawPixel(col,row,black);
  }
  void drawLine(int x1,int y1,int x2,int y2,bool black=true) const {
    assert(x1==x2 || y1==y2);
    fillRect(x1,y1,x2-x1+1,y2-y1+1,black);
  }
};
struct RasterTarget : freeink::ui::DrawTarget {
  bool pixels[40][40]{}; int orientation=0;
  void pixel(int x,int y,bool black) {
    assert(x>=0 && x<34 && y>=0 && y<24);
    switch(orientation) {
      case 1: pixels[x][39-y]=black; break;
      case 2: pixels[39-y][39-x]=black; break;
      case 3: pixels[39-x][y]=black; break;
      default: pixels[y][x]=black; break;
    }
  }
  bool pixel(int x,int y) const {
    switch(orientation) {
      case 1: return pixels[x][39-y];
      case 2: return pixels[39-y][39-x];
      case 3: return pixels[39-x][y];
      default: return pixels[y][x];
    }
  }
  freeink::ui::Size measureText(freeink::ui::FontId,const char*,freeink::ui::TextStyle) const override { return {}; }
  int16_t lineHeight(freeink::ui::FontId) const override { return 0; }
  void fill(freeink::ui::Rect r,freeink::ui::Paint paint,uint8_t,uint8_t) override {
    for(int y=r.y;y<r.bottom();++y) for(int x=r.x;x<r.right();++x)
      pixel(x,y,paint.color==freeink::ui::Color::Black);
  }
  void stroke(freeink::ui::Rect,freeink::ui::Paint,uint8_t,uint8_t,uint8_t) override { assert(false); }
  void line(freeink::ui::Point,freeink::ui::Point,uint8_t,freeink::ui::Paint) override { assert(false); }
  void triangle(freeink::ui::Point,freeink::ui::Point,freeink::ui::Point,freeink::ui::Paint) override { assert(false); }
  void text(freeink::ui::Rect,const char*,freeink::ui::TextStyle) override { assert(false); }
  void bitmap(freeink::ui::Rect,freeink::ui::BitmapRef,freeink::ui::BitmapMode,freeink::ui::Paint,freeink::ui::Rotation) override { assert(false); }
};
struct BaseTheme {
  static void drawBatteryOutline(const GfxRenderer&,int,int,int,int);
  static void drawBatteryLightningBolt(const GfxRenderer&,int,int);
  virtual void fillBatteryIcon(const GfxRenderer&,Rect,uint16_t) const;
  virtual void drawBatteryGlyph(freeink::ui::DrawTarget&,freeink::ui::Rect,uint16_t,bool) const;
};
struct LyraTheme : BaseTheme {
  void fillBatteryIcon(const GfxRenderer&,Rect,uint16_t) const override;
  void drawBatteryGlyph(freeink::ui::DrawTarget&,freeink::ui::Rect,uint16_t,bool) const override;
};
const BaseTheme* activeTheme=nullptr;
#define GUI (*activeTheme)
''' + method(source, 'void BaseTheme::drawBatteryOutline(')
            + method(source, 'void BaseTheme::drawBatteryLightningBolt(')
            + method(source, 'void BaseTheme::fillBatteryIcon(')
            + method(source, 'void BaseTheme::drawBatteryGlyph(')
            + method(lyra, 'void LyraTheme::fillBatteryIcon(')
            + method(lyra, 'void LyraTheme::drawBatteryGlyph(')
            + method(source, 'bool drawHeaderBattery(') + r'''
int main() {
  BaseTheme classic; LyraTheme lyra;
  for(const BaseTheme* theme : {&classic,static_cast<BaseTheme*>(&lyra)})
  for(int orientation=0;orientation<4;++orientation) for(bool charging : {false,true})
  for(uint16_t percent : {0,1,10,11,40,41,50,70,71,100,150,65535}) {
    activeTheme=theme; gpio.charging=charging;
    const int width=theme==&classic?15:16;
    GfxRenderer legacy;
    BaseTheme::drawBatteryOutline(legacy,0,0,width,12);
    theme->fillBatteryIcon(legacy,{0,0,width,12},percent);
    RasterTarget reader,header; reader.orientation=header.orientation=orientation;
    const freeink::ui::Rect glyph{0,0,static_cast<int16_t>((width+1)*2),24};
    theme->drawBatteryGlyph(reader,glyph,percent,charging);
    freeink::ui::DeviceContext device;
    freeink::ui::InputSnapshot input;
    freeink::ui::InteractionBuffer<1> interactions;
    freeink::ui::Frame<1> frame(header,device,input,interactions);
    freeink::ui::BatteryIndicatorProps props;
    props.percent=static_cast<uint8_t>(std::min<int>(percent,100));
    props.charging=charging; props.glyphWidth=glyph.width-2; props.glyphHeight=24;
    props.glyphPainter=drawHeaderBattery;
    freeink::ui::batteryIndicator(frame,glyph,props);
    for(int y=0;y<24;++y) for(int x=0;x<34;++x) {
      assert(reader.pixel(x,y)==legacy.pixels[y/2][x/2]);
      assert(header.pixel(x,y)==reader.pixel(x,y));
    }
  }
}
''', include_dirs=(ROOT / 'src', ROOT / 'freeink-sdk/libs/ui/FreeInkUI/include'),
                defines=('CROSSMUX_UI_PROFILE_HIGH_DPI',),
                extra_sources=(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp',))

    def test_footer_descenders_and_estimate_fit_the_status_line(self):
        faces = {}
        directory = ROOT / 'lib/EpdFont/builtinFonts'
        for name in ('notosans_12_regular', 'notosans_14_regular', 'notosans_cjk_12', 'ubuntu_12_regular'):
            source = (directory / f'{name}.h').read_text()
            metadata = source.split(f'static const EpdFontData {name} = {{')[1].split('};')[0].split(',')
            advance, ascender = int(metadata[4]), int(metadata[5])
            glyphs = rows(source, 'Glyphs')
            if name == 'notosans_cjk_12':
                source += (directory / 'notosans_cjk_common_intervals.h').read_text()
            intervals = rows(source, 'Intervals')
            lookup = {cp: glyphs[offset + cp - first] for first, last, offset in intervals
                      for cp in range(first, last + 1)}
            faces[name] = advance, ascender, lookup
        advance = faces['notosans_12_regular'][0]
        for name, chars in (('notosans_12_regular', 'gypjqQ'), ('notosans_cjk_12', '中阅读梦海'),
                            ('ubuntu_12_regular', 'אבג')):
            height, ascender, glyphs = faces[name]
            offset = int((advance - height) / 2)  # C++ truncates toward zero.
            for char in chars:
                glyph = glyphs[ord(char)]
                self.assertLessEqual(offset + ascender - glyph[4] + glyph[1], advance, char)
        estimate_advance, estimate_ascender, estimate_glyphs = faces['notosans_14_regular']
        marker = estimate_glyphs[ord('~')]
        marker_bottom = int((advance - estimate_advance) / 2) + estimate_ascender - marker[4] + marker[1]
        self.assertLessEqual(marker_bottom, advance)

    def test_reader_footer_geometry_with_all_status_fields(self):
        source = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
        layout_source = (ROOT / 'src/components/UITheme.cpp').read_text()
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>
struct Rect { int x,y,width,height; };
constexpr int READER_STATUS_FONT_ID=1, READER_ESTIMATE_FONT_ID=2, SMALL_FONT_ID=4;
struct CrossPointSettings {
 enum STATUS_BAR_PROGRESS_BAR { BOOK_PROGRESS=1 };
 enum { STATUS_BAR_CLOCK_HIDE=0,
        STATUS_BAR_CLOCK_LEFT=1, STATUS_BAR_CLOCK_RIGHT=2 };
 struct Spec {
   bool showBookProgressPercent=true,showChapterPageCount=true,showBattery=true,showBatteryPercent=true;
   int clockMode=STATUS_BAR_CLOCK_LEFT,progressBarMode=BOOK_PROGRESS,progressBarHeightPx=4;
   bool clock12h=false;
   bool showsProgressBar() const {return progressBarMode!=0;}
   bool showsClock() const {return clockMode!=0;}
   bool textLaneVisible() const {return true;}
 } spec;
 Spec statusBarSpec() const {return spec;}
} SETTINGS;
struct { uint16_t getBatteryPercentage() const {return 100;} } powerManager;
namespace bleinput { bool connected=false; bool isConnected() {return connected;} }
namespace TimeUtils { bool formatCurrentTime(char* text,size_t,bool) { std::strcpy(text,"23:59"); return true; } }
struct EpdFontFamily { enum { REGULAR }; };
struct Glyph { int top=0,height=0; };
struct Font { const Glyph* getGlyph(char,int) const {return nullptr;} };
namespace BoardConfig {
struct { int statusBarOffsetY=0,bottomCornerRadius=0,cornerRadius=0; } ACTIVE;
}
struct GfxRenderer {
 const std::map<int,Font>& getFontMap() const {static const std::map<int,Font> fonts;return fonts;}
 int getFontAscenderSize(int) const {return 34;}
 int width=684,height=1216,top=5,right=5,bottom=8,left=5;
 int footerLineHeight=34;
 mutable std::vector<Rect> texts;
 int getScreenWidth() const {return width;} int getScreenHeight() const {return height;}
 void getOrientedViewableTRBL(int* t,int* r,int* b,int* l) const {*t=top;*r=right;*b=bottom;*l=left;}
 int getLineHeight(int font) const {return font==READER_ESTIMATE_FONT_ID?40:font==READER_STATUS_FONT_ID?footerLineHeight:34;}
 int getTextWidth(int font,const char* text) const {return std::strlen(text)*(font==READER_ESTIMATE_FONT_ID?20:16);}
 void drawText(int font,int x,int y,const char* text) const {
   if (!*text) return;
   // The larger estimate face only draws '~' (32px ascender, 13px top, 5px ink height).
   Rect rect{x,font==READER_ESTIMATE_FONT_ID?y+19:y,getTextWidth(font,text),
             font==READER_ESTIMATE_FONT_ID?5:getLineHeight(font)};
   assert(x>=left+14 && x+rect.width<=width-right-14);
   assert(y>=height-bottom-58 && y+rect.height<=height-bottom-5);
   for (auto old : texts) assert(rect.x+rect.width<=old.x || old.x+old.width<=rect.x);
   texts.push_back(rect);
 }
 std::string truncatedText(int,const char*,int maxWidth) const {
   assert(maxWidth>0); return std::string(maxWidth/16,'L');
 }
 void fillRect(int x,int y,int w,int h,bool=true) const {
   assert(x>=0 && x+w<=width && y>=0 && y+h<=height);
 }
};
namespace freeink::ui {
struct Rect { int x,y,width,height; };
struct DrawTarget {};
struct GfxRendererTarget : DrawTarget {
 const GfxRenderer& renderer;
 explicit GfxRendererTarget(const GfxRenderer& r):renderer(r) {}
};
}
struct { bool isUsbConnected() const { return false; } } gpio;
struct Metrics { int batteryWidth=34,batteryHeight=24,statusBarHorizontalMargin=14,statusBarVerticalMargin=48; };
struct BaseTheme {
 static constexpr int STATUS_NUMERIC_FONT_ID=3,batteryPercentSpacing=4;
 void drawBatteryLeft(const GfxRenderer&,Rect,bool) const;
 static void drawBatteryOutline(const GfxRenderer& r,int x,int y,int w,int h) {
   assert(w==34 && h==24 && x>=r.left+14 && x+w<=r.width-r.right-14);
   assert(y>=r.height-r.bottom-58 && y+h<=r.height-r.bottom-5);
 }
 void fillBatteryIcon(const GfxRenderer&,Rect,uint16_t) const {}
 void drawBatteryGlyph(freeink::ui::DrawTarget& target,freeink::ui::Rect rect,uint16_t,bool) const {
   drawBatteryOutline(static_cast<freeink::ui::GfxRendererTarget&>(target).renderer,rect.x,rect.y,rect.width,rect.height);
 }
 static void drawStatusBar(GfxRenderer&,float,int,int,std::string,int,int,bool,bool,bool);
};
struct UITheme {
 inline static bool inx=false;
 bool hasMainTabs() const {return inx;}
 static UITheme& getInstance() {static UITheme theme;return theme;}
 Metrics getMetrics() const {return {};}
 BaseTheme& getTheme() {static BaseTheme theme;return theme;}
 int getStatusBarHeight() const {return 48+10;}
 static int getStatusBarTextTopPadding(const GfxRenderer&);
};
#define GUI UITheme::getInstance().getTheme()
constexpr int bookmarkStatusIconWidth=24,bookmarkStatusIconGap=12,bluetoothStatusIconWidth=24;
void drawBookmarkStatusIcon(const GfxRenderer& r,int x,int y) {
 assert(x>=r.left+14 && x+24<=r.width-r.right-14 && y+24<=r.height-r.bottom-5);
}
void drawBluetoothStatusIcon(const GfxRenderer& r,int x,int y) {drawBookmarkStatusIcon(r,x,y);}
''' + method(layout_source, 'int UITheme::getStatusBarTextTopPadding(')
            + method(source, 'void BaseTheme::drawBatteryLeft(')
            + method(source, 'void BaseTheme::drawStatusBar(') + r'''
int main() {
 for (auto size : {Rect{0,0,684,1216},Rect{0,0,600,1000}})
 for (bool inx : {false,true})
 for (int orientation=0;orientation<4;++orientation) for (bool percent : {false,true})
 for (int clock : {CrossPointSettings::STATUS_BAR_CLOCK_HIDE,CrossPointSettings::STATUS_BAR_CLOCK_LEFT,
                   CrossPointSettings::STATUS_BAR_CLOCK_RIGHT}) for(bool markers : {false,true})
 for(int footerLineHeight : {26,34,42}) {
   GfxRenderer r; UITheme::inx=inx;
   r.footerLineHeight=footerLineHeight;
   r.width=size.width;r.height=size.height;
   if (orientation%2) {r.width=size.height;r.height=size.width;}
   if (orientation==1) {r.top=5;r.right=8;r.bottom=5;r.left=5;}
   if (orientation==2) {r.top=8;r.right=5;r.bottom=5;r.left=5;}
   if (orientation==3) {r.top=5;r.right=5;r.bottom=5;r.left=8;}
   SETTINGS.spec.showBatteryPercent=percent;SETTINGS.spec.clockMode=clock;bleinput::connected=markers;
   BaseTheme::drawStatusBar(r,100,65535,65535,std::string(200,'L'),0,0,false,markers,true);
   assert(!r.texts.empty());
   assert(r.texts[1].y+std::max(footerLineHeight,34)==r.height-r.bottom-10-UiHighDpiProfile::readerStatusBottomPadding);
 }
}
''', defines=('CROSSMUX_UI_PROFILE_HIGH_DPI','FREEINK_DEVICE_READPICO=1'))

    def test_reader_content_reclaims_footer_padding_without_moving_text(self):
        layout_source = (ROOT / 'src/components/UITheme.cpp').read_text()
        reader_source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        render = method(reader_source, 'void EpubReaderActivity::renderBook(')
        start = render.index('  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;')
        end = render.index('  const uint16_t viewportWidth =', start)
        program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
constexpr int READER_STATUS_FONT_ID=1, SMALL_FONT_ID=2;
struct BaseTheme { static constexpr int STATUS_NUMERIC_FONT_ID=3; };
struct Metrics { int statusBarVerticalMargin=UiHighDpiProfile::enabled?48:19, progressBarMarginTop=1; };
using ThemeMetrics=Metrics;
struct Settings {
 uint8_t screenMargin=25;
 struct Spec {
   bool text=true, progress=false;
   int progressBarHeightPx=4;
   bool textLaneVisible() const {return text;}
   bool showsProgressBar() const {return progress;}
 } spec;
 Spec statusBarSpec() const {return spec;}
} SETTINGS;
struct GfxRenderer {
 int width=684,height=1216,top=5,right=5,bottom=8,left=5;
 int textHeight=34, numericHeight=34;
 int getLineHeight(int font) const {return font==BaseTheme::STATUS_NUMERIC_FONT_ID?numericHeight:textHeight;}
 void getOrientedViewableTRBL(int* t,int* r,int* b,int* l) const {*t=top;*r=right;*b=bottom;*l=left;}
};
struct UITheme {
 static UITheme& getInstance() {static UITheme theme;return theme;}
 Metrics getMetrics() const {return {};}
 static int getStatusBarHeight();
 static int getProgressBarHeight();
 static int getStatusBarTextTopPadding(const GfxRenderer&);
};
''' + method(layout_source, 'int UITheme::getStatusBarHeight(') + method(layout_source, 'int UITheme::getProgressBarHeight(') + method(layout_source, 'int UITheme::getStatusBarTextTopPadding(') + r'''
int contentBottom(const GfxRenderer& renderer,bool automaticPageTurnActive) {
''' + render[start:end] + r'''
 return renderer.height-orientedMarginBottom;
}
int main() {
 for (int orientation=0;orientation<4;++orientation)
 for (int textHeight : {26,34,42,48,56}) for (int numericHeight : {34,50})
 for (bool text : {false,true}) for (bool progress : {false,true})
 for (bool automatic : {false,true}) for (uint8_t margin : {0,25,100}) {
   GfxRenderer r;
   if(orientation%2) {r.width=1216;r.height=684;}
   if(orientation==1) {r.right=8;r.bottom=5;}
   if(orientation==2) {r.top=8;r.bottom=5;}
   if(orientation==3) {r.left=8;r.bottom=5;}
   r.textHeight=textHeight;r.numericHeight=numericHeight;
   SETTINGS.screenMargin=margin;SETTINGS.spec.text=text;SETTINGS.spec.progress=progress;
   const int lane=UITheme::getInstance().getMetrics().statusBarVerticalMargin;
   const int bar=progress?5:0;
   const int oldReserve=(text||automatic?lane:0)+bar;
   const int oldBottom=r.height-r.bottom-std::max<int>(margin,oldReserve);
   const int bottom=contentBottom(r,automatic);
   assert(bottom>=oldBottom);
   assert(bottom<=r.height-r.bottom-margin);
   if(!UiHighDpiProfile::enabled || !(text||automatic) || margin>=oldReserve)
     assert(bottom==oldBottom);
   if(UiHighDpiProfile::enabled && (text||automatic)) {
     const int offset=UITheme::getStatusBarTextTopPadding(r);
     const int textY=r.height-r.bottom-oldReserve+offset;
     assert(textY-bottom>=std::min(6,offset));
     if(textHeight==34 && numericHeight==34 && margin==25) {
       assert(bottom-oldBottom==7);
       assert(textY+34==r.height-r.bottom-bar-1);
     }
     if(std::max(textHeight,numericHeight)>=42) assert(bottom==oldBottom);
   }
 }
}
'''
        for defines in (('CROSSMUX_UI_PROFILE_HIGH_DPI', 'FREEINK_DEVICE_READPICO=1'), ()):
            run_cpp(program, defines=defines)


if __name__ == '__main__':
    unittest.main()
