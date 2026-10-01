#!/usr/bin/env python3
"""Check production legacy-list rendering and touch with actual SDK layout."""
from pathlib import Path
import tempfile
from test_theme_menus import method, run
root = Path(__file__).resolve().parents[2]
base = (root / 'src/components/themes/BaseTheme.cpp').read_text()
touch = (root / 'src/MappedInputManager.cpp').read_text()
program = r'''
#include <FreeInkApp.h>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
namespace fui=freeink::ui;
struct Rect {int x,y,width,height;};
enum UIIcon {None};
struct GfxRenderer {int getScreenWidth()const{return 480;} int getScreenHeight()const{return 800;}} renderer;
struct CrossPointSettings {static constexpr int INX=5; int uiTheme=1;} SETTINGS;
struct Metrics {int listRowHeight=40,listWithSubtitleRowHeight=60,listRowGap=6,listInset=24;} metrics;
namespace BoardConfig {bool touch=true; bool hasTouch(){return touch;}}
struct BaseTheme {
 mutable std::atomic<int> listRowStep_[2]{};
 int getListRowStep(bool)const;
 int getListPageItems(int,bool)const;
 void drawList(const GfxRenderer&,Rect,int,int,const std::function<std::string(int)>&,
 const std::function<std::string(int)>&,const std::function<UIIcon(int)>&,
 const std::function<std::string(int)>&,bool,const std::function<bool(int)>&,bool,
 const std::function<bool(int)>&)const;
} theme;
struct UITheme {
 static UITheme& getInstance(){static UITheme ui;return ui;}
 const Metrics& getMetrics()const{return metrics;}
 const BaseTheme& getTheme()const{return theme;}
};
struct MappedInputManager {
 const GfxRenderer& renderer=::renderer;
 bool hasTouch()const{return BoardConfig::touch;}
 bool listItemFromPoint(int,int,int&,int,int,int,int,bool)const;
} input;
struct Text {fui::Rect rect; std::string label;};
std::vector<Text> drawn;
int fontHeight=24;
class Target:public fui::DrawTarget {
 public:
 fui::Size measureText(fui::FontId,const char* s,fui::TextStyle)const override{return {int16_t(strlen(s)*8),int16_t(fontHeight)};}
 int16_t lineHeight(fui::FontId)const override{return fontHeight;}
 void fill(fui::Rect,fui::Paint,uint8_t,uint8_t)override{}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t)override{}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation)override{}
 void text(fui::Rect r,const char* text,fui::TextStyle)override{if(std::string(text).starts_with("row "))drawn.push_back({r,text});}
};
namespace freeink::ui {
template<size_t N>struct GfxRendererFrame {
 Target target;DeviceContext device;InputSnapshot input;InteractionBuffer<N> interactions;Frame<N> frame;
 GfxRendererFrame(const GfxRenderer&,int,int,int):device(),frame(target,device,input,interactions){device.width=480;device.height=800;device.hasTouch=BoardConfig::touch;}
};
}
struct Fonts {int smallFontId=1,bodyFontId=2,titleFontId=2;};
Fonts uiScaleSpec(){return {};}
void applyUiTextAlignment(Target&){}
const fui::ThemeTokens& refreshSharedUiThemeTokens(const Target&){
 static fui::ThemeTokens tokens;tokens=fui::themeTokensForLineHeight(fontHeight);
 tokens.listInset=metrics.listInset;tokens.listRowGap=metrics.listRowGap;
 return tokens;
}
fui::BitmapRef listIconFor(UIIcon){return {};}
@METHODS@
int main(){
 for(bool touch:{false,true})for(bool subtitle:{false,true})for(int font:{20,34})for(int selected:{0,3,9,11}){
  BoardConfig::touch=touch;fontHeight=font;drawn.clear();theme.listRowStep_[0]=theme.listRowStep_[1]=0;
  std::function<std::string(int)> sub=subtitle?std::function<std::string(int)>([](int){return "subtitle";}):nullptr;
  theme.drawList(renderer,{0,120,480,480},12,selected,[](int i){return "row "+std::to_string(i);},sub,nullptr,[](int){return "value";},false,nullptr,true,nullptr);
  const int step=theme.getListRowStep(subtitle),page=theme.getListPageItems(480,subtitle),first=selected/page*page;
  assert(!drawn.empty()&&drawn.front().label=="row "+std::to_string(first));
  assert(int(drawn.size())==std::min(page,12-first));
  for(const auto& text:drawn){int index=-1;assert(input.listItemFromPoint(text.rect.x+5,text.rect.y+text.rect.height/2,index,12,selected,120,480,subtitle));assert(text.label=="row "+std::to_string(index));}
  int hit=-1;assert(!input.listItemFromPoint(100,120+step-1,hit,12,selected,120,480,subtitle));
  assert(!input.listItemFromPoint(0,130,hit,12,selected,120,480,subtitle));
 }
}
'''
program = program.replace('@METHODS@', '\n'.join(method(base, x) for x in [
    'BaseTheme::getListRowStep', 'BaseTheme::getListPageItems', 'BaseTheme::drawList']) + '\n' + method(touch, 'MappedInputManager::listItemFromPoint'))
with tempfile.TemporaryDirectory(prefix='legacy-list-') as directory:
    run(program, Path(directory), sdk=True)
print('Production legacy list: fonts, touch/button cadence, subtitles, last page and noninteractive gaps pass')
