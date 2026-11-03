#!/usr/bin/env python3
"""Production slider draw/hit parity; historical INX page baselines stay unchanged."""
from pathlib import Path
import re,tempfile
from test_theme_menus import run,method
root=Path(__file__).resolve().parents[2]
here=Path(__file__).resolve().parent
text=(root/'src/components/UiSliderDialog.h').read_text()
trace=(here/'InxStyleParity.cpp').read_text().split('#ifdef UPSTREAM_THEME_PARITY')[0]
fields=sorted(set(re.findall(r'metrics\.(\w+)',text)))+['capsuleRadius']
lyra=(root/'src/components/themes/lyra/LyraTheme.h').read_text()
metrics='struct Metrics {'+''.join('int '+k+'='+re.search(r'\.'+k+r'\s*=\s*([^,}]+)',lyra)[1]+';' for k in fields)+'};'
spec=text[text.index('struct UiSliderDialogSpec'):text.index('inline void buildSliderDialogScreen')]
body=method(text,'buildSliderDialogScreen')
code=trace+r'''
#include <cassert>
#include <string>
namespace fui=freeink::ui;
struct CrossPointSettings {static constexpr int INX=5;int uiTheme=1;} SETTINGS;
constexpr int UI_10_FONT_ID=10,UI_12_FONT_ID=12,CONTROL_18_FONT_ID=18,NOTOSANS_18_FONT_ID=118;
constexpr int STR_CONFIRM=1;
const char* tr(int){return "Confirm";}
namespace freeink::ui {
struct GfxRendererTarget:TraceTarget {
 static constexpr FontId FONT_SMALL=0,FONT_BODY=1,FONT_TITLE=2;
 int bound[3]{};
 void setFont(FontId slot,int id){bound[slot]=id;}
 void setTextCentering(TextCentering){}
};
}
const fui::ThemeTokens& refreshSharedUiThemeTokens(const fui::GfxRendererTarget& target){
 static fui::ThemeTokens tokens;tokens=fui::themeTokensForLineHeight(target.lineHeight(1));return tokens;
}
@METRICS@
namespace LyraMetrics {const Metrics values;}
struct UITheme {static UITheme&getInstance(){static UITheme t;return t;}const Metrics&getMetrics(){return LyraMetrics::values;}};
const Metrics& uiThemeMetrics(bool=false){return UITheme::getInstance().getMetrics();}
struct UiAppHost {using UiScreen=fui::Screen<24>;};
struct MappedInputManager {bool touch=true;bool hasTouch()const{return touch;}};
@SPEC@
@BODY@
int main(int argc,char**argv){
 SETTINGS.uiTheme=argc>1?5:1;int scene=0;
 for(bool landscape:{false,true})for(bool touch:{false,true})for(int value:{0,1,50,99,100}){
  std::printf("SCENE %d\n",scene++);
  fui::GfxRendererTarget target;target.lineH=34;target.scaledFonts=true;
  DeviceContext device;device.width=landscape?800:480;device.height=landscape?480:800;device.hasTouch=touch;
  InteractionBuffer<24> hits;InputSnapshot input;Frame<24> frame(target,device,input,hits);
  auto tokens=themeTokensForLineHeight(target.lineHeight(1));tokens.capsuleRadius=6;
  fui::Screen<24> screen(frame,tokens);MappedInputManager mapped{touch};
  UiSliderDialogSpec spec;spec.title="Setting";spec.readout="50 min";spec.value=value;spec.max=100;
  spec.minLabel="0";spec.maxLabel="100";spec.sliderAction=1;spec.stepAction=2;spec.okAction=3;spec.chromeAction=4;
  spec.hintLine1="Front +/-1";spec.hintLine2="Side +/-10";
  buildSliderDialogScreen(screen,target,mapped,spec);
  assert(target.bound[2]==(argc>1?CONTROL_18_FONT_ID:NOTOSANS_18_FONT_ID));
  if(argc>1)assert(target.bound[0]==UI_10_FONT_ID&&target.bound[1]==UI_12_FONT_ID);
  for(size_t i=0;i<hits.count();++i){const auto&h=hits.data()[i];std::printf("hit %d %d %d %d %d %d\n",h.rect.x,h.rect.y,h.rect.width,h.rect.height,h.action,h.value);}
 }
}
'''.replace('@METRICS@',metrics).replace('@SPEC@',spec).replace('@BODY@',body)
with tempfile.TemporaryDirectory(prefix='inx-controls-') as d:
    import subprocess
    d=Path(d);lyra_trace=run(code,d,sdk=True)
    inx_trace=subprocess.check_output([str(d/'check'),'inx'],text=True)
    if inx_trace!=lyra_trace:
        import difflib
        print(''.join(list(difflib.unified_diff(lyra_trace.splitlines(True),inx_trace.splitlines(True)))[:30]))
    assert inx_trace==lyra_trace,'INX slider differs from the common slider draw/hit trace'
print('INX setting slider: 20 portrait/landscape, touch/button and boundary-value draw/hit traces match Lyra')
