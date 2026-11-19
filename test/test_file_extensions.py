"""Compile the production row provider; display toggles must never rename files."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    # Skip the forward declarations above the definitions.
    while ";" in source[start:opening]:
        start = source.index(signature, start + 1)
        opening = source.index("{", start)
    end = opening + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

source = (ROOT / "src/activities/home/FileBrowserActivity.cpp").read_text()
code = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <Utf8.h>
namespace fui {struct ListItem {const char* label=nullptr;const char* value=nullptr;int icon=0;int16_t actionValue=-1;};}
struct UITheme {
 bool icons=true;
 static UITheme& getInstance(){static UITheme theme;return theme;}
 const UITheme& getTheme()const{return *this;}
 bool showsFileIcons()const{return icons;}
 static int getFileIcon(const std::string&){return 7;}
};
int listIconFor(int icon){return icon;}
#define STR_MOVE_HERE "Move here"
#define tr(x) (x)
constexpr const char* MOVE_HERE_ENTRY="\x01";
struct {bool showFileExtensions=true;} SETTINGS;
struct FileBrowserActivity {
 std::vector<std::string> files;
 char rowNameBuf[128]{},rowExtBuf[32]{};
 static void provideRow(void*,uint16_t,fui::ListItem&);
};
""" + "\n".join(method(source, name) for name in [
    "void formatFileName(", "void formatFileExtension(", "void FileBrowserActivity::provideRow("])
code += r"""
int main(){
 FileBrowserActivity browser;
 browser.files={"book.epub","book.xtc",".epub"," .epub","folder/","README","cafe\xcc\x81.epub",MOVE_HERE_ENTRY};
 const auto original=browser.files;
 for(bool icons:{false,true})for(bool enabled:{false,true}){
  UITheme::getInstance().icons=icons;SETTINGS.showFileExtensions=enabled;
  for(size_t i=0;i<browser.files.size();++i){
   fui::ListItem row;FileBrowserActivity::provideRow(&browser,i,row);
   assert(row.label!=nullptr&&row.actionValue==static_cast<int16_t>(i));
   if(i==0||i==1||i==6)assert((row.value!=nullptr)==enabled);
   if(i==2||i==3)assert(row.value&&std::string(row.value)==".epub");
   if(i==4)assert(std::string(row.label)==(icons?"folder":"[folder]")&&row.value==nullptr);
   if(i==5)assert(std::string(row.label)=="README"&&row.value==nullptr);
   if(i==6)assert(std::string(row.label)=="caf\xc3\xa9");
   if(i==7)assert(std::string(row.label)=="Move here");
  }
 }
 fui::ListItem reused;
 SETTINGS.showFileExtensions=true;FileBrowserActivity::provideRow(&browser,0,reused);
 assert(reused.value);
 SETTINGS.showFileExtensions=false;FileBrowserActivity::provideRow(&browser,0,reused);
 assert(reused.value==nullptr);
 assert(browser.files==original);
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"extensions.cpp"
    binary=Path(directory)/"extensions"
    cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-I",str(ROOT/"lib/Utf8"),str(cpp),str(ROOT/"lib/Utf8/Utf8.cpp"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Production extension visibility, extension-only names, NFC display, folder style and unchanged paths pass")
