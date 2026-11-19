"""Compile production Home/transfer/Back routing after OPDS migration."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

home = (ROOT / "src/activities/home/HomeActivity.cpp").read_text()
manager = (ROOT / "src/activities/ActivityManager.h").read_text()
mode = (ROOT / "src/activities/network/NetworkModeSelectionActivity.cpp").read_text()
browser = (ROOT / "src/activities/browser/OpdsBookBrowserActivity.cpp").read_text()
menus = home[home.index("struct HomeMenuEntry {"):home.index("}  // namespace")]
strids = sorted(set(re.findall(r"StrId::(\w+)", menus)))
icons = sorted(set(re.findall(r", (\w+)},", menus)))
code = r"""
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
""" + re.search(r"enum class HomeMenuItem[^;]+;", manager).group() + "\n"
code += "enum class StrId {" + ",".join(strids) + "};\n"
code += "enum UIIcon {" + ",".join(icons) + "};\n" + menus + r"""
constexpr unsigned long GO_HOME_MS=800;
struct OpdsBookBrowserActivity {
 struct {unsigned long held=0;unsigned long getHeldTime()const{return held;}} mappedInput;
 std::vector<std::string> navigationHistory,searchQueryHistory;
 std::string currentPath,query;
 unsigned homes=0,browses=0;
 void onGoHome(){++homes;}
 void setSearchQuery(const std::string& s){query=s;}
 void startBrowse(){++browses;}
 void navigateBack();
};
""" + method(browser,"void OpdsBookBrowserActivity::navigateBack()") + r"""
int main(){
 for(bool carousel:{false,true})for(bool plugins:{false,true}){
  int count=plugins?kHomeMenuItemCount:kHomeMenuItemCount-1;
  int apps=0,pluginRows=0,transfer=0;
  for(int i=0;i<count;++i){
   auto item=indexToMenuItem(i,plugins,carousel);
   assert(item!=HomeMenuItem::OPDS_BROWSER);
   apps+=item==HomeMenuItem::APPS;pluginRows+=item==HomeMenuItem::PLUGINS;
   transfer+=item==HomeMenuItem::FILE_TRANSFER;
   assert(menuItemToIndex(item,plugins,carousel)==i);
  }
  assert(apps==1&&transfer==1&&pluginRows==plugins);
  auto focus=menuItemToIndex(HomeMenuItem::OPDS_BROWSER,plugins,carousel);
  assert(indexToMenuItem(focus,plugins,carousel)==HomeMenuItem::FILE_TRANSFER);
 }
 for(bool history:{false,true})for(unsigned long held:{0ul,GO_HOME_MS-1,GO_HOME_MS,GO_HOME_MS+1}){
  OpdsBookBrowserActivity a;
  if(history){a.navigationHistory={"parent"};a.searchQueryHistory={"search"};}
  a.mappedInput.held=held;a.navigateBack();
  const bool home=!history||held>=GO_HOME_MS;
  assert(a.homes==home&&a.browses==!home);
  if(!home)assert(a.currentPath=="parent"&&a.query=="search"&&a.navigationHistory.empty());
 }
}
"""
# Every declared transfer row is inserted, independent of plugins/server configuration.
constructor = method(mode,"NetworkModeSelectionActivity::NetworkModeSelectionActivity(")
assert "SETTINGS" not in constructor and "OPDS_STORE" not in constructor
assert "if (row.mode" not in constructor and "NetworkMode::OPDS_BROWSER" in mode
assert "hasOpdsServers" not in home
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "navigation.cpp"
    binary = Path(directory) / "navigation"
    source.write_text(code)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Production Home plugin toggle/APP/return focus, always-visible OPDS transfer row and short/held Back routing pass")
