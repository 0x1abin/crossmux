"""Production notice parsing/persistence/open/uninstall with real JSON and MD5."""
from pathlib import Path
import os
import sys
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
JSON=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/"build/test/_deps/arduinojson-src/src"
SIM=Path(sys.argv[2]) if len(sys.argv)>2 else ROOT/"build/test/_deps/crosspoint_simulator-src"
def method(source,name):
    start=source.index(name);end=source.index("{",start)+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]
source=(ROOT/"src/activities/plugins/PluginCatalogActivity.cpp").read_text()
header=(ROOT/"src/activities/plugins/PluginCatalogActivity.h").read_text()
static=source[source.index('  const size_t fixedLists = browse["lists"].size();'):source.index('  for (JsonVariantConst l : browse["lists"]')]
dynamic=source[source.index('  const size_t allLists = manifest.fixedListCount'):source.index('  for (JsonVariantConst l : lists)',source.index('  const size_t allLists = manifest.fixedListCount'))]
assert source.count('entry.noticeId = persistentNoticeId(notice);')==2
code=r"""
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <Memory.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "util/PluginLocations.h"
unsigned errors=0;
#define LOG_ERR(...) (++errors)
constexpr size_t MAX_CATALOG_BYTES=48*1024,CATALOG_HEAP_RESERVE=32*1024,MAX_BROWSE_LISTS=64;
struct{size_t available=1000000,largest=1000000;size_t getFreeHeap()const{return available;}size_t getMaxAllocHeap()const{return largest;}}ESP;
struct HalFile;
struct StorageFixture {
 struct StorageLock{~StorageLock(){}};
 std::map<std::string,std::string> files;
 std::set<std::string> dirs,removeFailures;
 bool openFail=false,closeFail=false,renameFail=false,mkdirFail=false,readFail=false;
 size_t budget=SIZE_MAX;
 bool exists(const char*p)const{if(files.contains(p)||dirs.contains(p))return true;const std::string prefix=std::string(p)+"/";return std::any_of(files.begin(),files.end(),[&](const auto&e){return e.first.starts_with(prefix);});}
 bool remove(const char*p){if(removeFailures.contains(p))return false;return files.erase(p);}
 bool removeDir(const char*p){
  if(removeFailures.contains(p))return false;
  const std::string prefix=std::string(p)+"/";
  std::erase_if(files,[&](const auto&e){return e.first.starts_with(prefix);});
  std::erase_if(dirs,[&](const auto&e){return e==p||e.starts_with(prefix);});return true;
 }
 bool rename(const char*a,const char*b){if(renameFail||!files.contains(a)||files.contains(b))return false;files[b]=std::move(files[a]);files.erase(a);return true;}
 bool ensureDirectoryExists(const char*p){if(mkdirFail)return false;dirs.insert(p);return true;}
 bool openFileForRead(const char*,const std::string&,HalFile&);
 bool openFileForWrite(const char*,const std::string&,HalFile&);
}Storage;
using HalStorage=StorageFixture;
struct HalFile {
 std::string path;
 size_t size()const{return Storage.files.at(path).size();}
 int read(void*b,size_t n){if(Storage.readFail)return -1;const auto&s=Storage.files.at(path);n=std::min(n,s.size());memcpy(b,s.data(),n);return n;}
 size_t write(const uint8_t*b,size_t n){size_t count=std::min(n,Storage.budget);Storage.budget-=count;Storage.files[path].append(reinterpret_cast<const char*>(b),count);return count;}
 bool close(){return !Storage.closeFail;}
};
bool StorageFixture::openFileForRead(const char*,const std::string&p,HalFile&f){if(readFail||!files.contains(p))return false;f.path=p;return true;}
bool StorageFixture::openFileForWrite(const char*,const std::string&p,HalFile&f){if(openFail)return false;files[p].clear();f.path=p;return true;}
"""+"\n".join(method(source,n) for n in ['bool catalogHasSpace(','std::string md5Hex(','std::string pluginNameFromManifestPath(','std::string persistentNoticeId(','std::string noticeMarkerDir(','std::string noticeMarkerPath(','bool noticeAcknowledged(','bool rememberNotice('])+r"""
bool allocationOk=true;
#define makeUniqueNoThrow testMakeUniqueNoThrow
template<class T,class...A>auto makeUniqueNoThrow(A&&...args){return allocationOk?std::make_unique<T>(std::forward<A>(args)...):nullptr;}
struct ConfirmationActivity{template<class...A>ConfirmationActivity(A&&...){}};
struct ActivityResult{bool isCancelled=false;};
constexpr const char* STR_PLUGIN_UNINSTALL_CONFIRM="Uninstall %s?";
constexpr const char* STR_PLUGIN_UNINSTALL_TITLE="Uninstall";
constexpr const char* STR_CANCEL="Cancel",*STR_PLUGIN_UNINSTALL="Uninstall";
const char*tr(const char*s){return s;}
struct PluginRef{std::string name,dir,title;};
struct PluginCatalogActivity {
 struct Manifest {
"""+method(header,"struct BrowseList")+r""";
  std::vector<BrowseList>browseLists;size_t fixedListCount=0;
 }manifest;
 enum class State{PLUGIN_PICKER,BROWSING};State state=State::PLUGIN_PICKER;
 std::string manifestPath="/plugins/demo/device.json",pickerReturnName;
 std::vector<PluginRef>installedPlugins;
 bool showPluginHubInstallRow=false;int currentList=-1,browsed=0,exited=0,pickers=0;
 int renderer=0,mappedInput=0;
 struct{void clearTapFlash(){}}app;
 std::function<void(const ActivityResult&)>callback;
 std::string substituted(const std::string&s,void*){return s;}
 void startBrowse(){++browsed;}void exitCatalog(){++exited;}void enterPluginPicker(){++pickers;}
 template<class T>void startActivityForResult(std::unique_ptr<T>,std::function<void(const ActivityResult&)>c){callback=std::move(c);}
 void openList(int);void confirmUninstallPlugin(int);
 bool reserveFixed(JsonVariantConst browse){
"""+static+r"""
 return true;}
 bool reserveDynamic(JsonArrayConst lists){
"""+dynamic+r"""
 return true;}
};
"""+method(source,"void PluginCatalogActivity::openList(")+"\n"+method(source,"void PluginCatalogActivity::confirmUninstallPlugin(")+r"""
PluginCatalogActivity catalog(){PluginCatalogActivity p;PluginCatalogActivity::Manifest::BrowseList list;
 list.title="List";list.noticeMessage="Notice";list.noticeId="guidance";p.manifest.browseLists.push_back(list);return p;}
void reset(){Storage={};errors=0;ESP={};allocationOk=true;}
int main(){
 for(const char*text:{"{}","{\"id\":\"x\",\"once\":1}","{\"id\":\"x\",\"once\":\"true\"}","{\"id\":7,\"once\":true}","{\"id\":\"\",\"once\":true}","{\"id\":\"a/b\",\"once\":true}","{\"id\":\"é\",\"once\":true}"}){
  JsonDocument d;assert(deserializeJson(d,text)==DeserializationError::Ok);assert(persistentNoticeId(d.as<JsonVariantConst>()).empty());
 }
 for(size_t length:{size_t(1),size_t(64),size_t(65)}){JsonDocument d;std::string id(length,'A');d["id"]=id;d["once"]=true;assert(persistentNoticeId(d.as<JsonVariantConst>())==(length<=64?id:std::string{}));}
 reset();auto p=catalog();const std::string marker=noticeMarkerPath(p.manifestPath,"guidance"),temp=marker+".tmp";
 assert(marker=="/.crosspoint/notice_"+md5Hex("/plugins/demo")+"/"+md5Hex("guidance")+".ok");
 for(const char*root:PluginLocations::ROOTS){auto a=noticeMarkerPath(std::string(root)+"/demo/device.json","guidance");assert(!a.empty());if(std::string(root)!="/plugins")assert(a!=marker);}
 p.openList(0);assert(p.callback&&p.browsed==0&&Storage.files.empty());p.callback({true});assert(Storage.files.empty()&&p.exited==1);
 p=catalog();p.openList(0);p.callback({false});assert(p.browsed==1&&Storage.files[marker]=="1"&&!Storage.files.contains(temp));
 auto same=catalog();same.openList(0);assert(same.browsed==1&&!same.callback);
 same.manifest.browseLists[0].title="Updated title";same.openList(0);assert(same.browsed==2&&!same.callback);
 for(int fault=0;fault<6;++fault){
  reset();auto a=catalog();Storage.openFail=fault==0;Storage.budget=fault==1?0:SIZE_MAX;Storage.closeFail=fault==2;Storage.renameFail=fault==3;Storage.mkdirFail=fault==4;Storage.readFail=fault==5;
  a.openList(0);a.callback({false});assert(a.browsed==1&&!Storage.files.contains(marker));
  Storage.openFail=Storage.closeFail=Storage.renameFail=Storage.mkdirFail=Storage.readFail=false;Storage.budget=SIZE_MAX;
  auto retry=catalog();retry.openList(0);assert(retry.callback&&retry.browsed==0);
 }
 for(const auto&bad:std::vector<std::string>{"","11","1\n","user notes"}){
  reset();Storage.files[marker]=bad;auto before=Storage.files;auto a=catalog();a.openList(0);assert(a.callback);a.callback({false});assert(Storage.files==before&&a.browsed==1);
 }
 reset();Storage.files[temp]="1";auto pending=catalog();pending.openList(0);assert(pending.callback&&pending.browsed==0);pending.callback({false});assert(Storage.files[marker]=="1");
 reset();Storage.files[temp]="user notes";assert(!rememberNotice(marker)&&Storage.files[temp]=="user notes"&&!Storage.files.contains(marker));
 reset();Storage.files[marker]="1";Storage.readFail=true;assert(!noticeAcknowledged(marker));
 reset();allocationOk=false;auto oom=catalog();oom.openList(0);assert(oom.exited==1&&oom.browsed==0&&!oom.callback&&Storage.files.empty());
 reset();auto repeated=catalog();repeated.manifest.browseLists[0].noticeId.clear();repeated.openList(0);repeated.callback({false});assert(repeated.browsed==1&&Storage.files.empty());
 for(bool cancel:{false,true})for(bool failed:{false,true}){
  reset();auto a=catalog();a.installedPlugins.push_back({"demo","/plugins/demo","Demo"});Storage.dirs.insert("/plugins/demo");Storage.files[marker]="1";
  if(failed)Storage.removeFailures.insert("/plugins/demo");a.confirmUninstallPlugin(0);assert(a.callback);a.callback({cancel});
  assert(Storage.files.contains(marker)==(cancel||failed));
  if(!cancel&&!failed){assert(!Storage.dirs.contains("/plugins/demo"));auto fresh=catalog();fresh.openList(0);assert(fresh.callback&&fresh.browsed==0);}
 }
 reset();auto broken=catalog();broken.installedPlugins.push_back({"demo","/plugins/demo","Demo"});Storage.dirs.insert("/plugins/demo");Storage.files[marker]="1";Storage.removeFailures.insert(noticeMarkerDir("/plugins/demo"));broken.confirmUninstallPlugin(0);broken.callback({false});assert(Storage.files.contains(marker)&&errors==1);
 reset();auto unsafe=catalog();unsafe.installedPlugins.push_back({"../demo","/plugins/../demo","Unsafe"});unsafe.confirmUninstallPlugin(0);assert(!unsafe.callback&&errors==1);
 JsonDocument d;auto lists=d["lists"].to<JsonArray>();for(int i=0;i<2;++i)lists.add(JsonObject());
 auto budget=catalog();budget.manifest.browseLists.clear();assert(budget.reserveFixed(d.as<JsonVariantConst>()));
 ESP.available=CATALOG_HEAP_RESERVE;assert(!budget.reserveFixed(d.as<JsonVariantConst>()));
 ESP={};ESP.largest=4096;assert(!budget.reserveDynamic(lists));
 ESP={};budget.manifest.fixedListCount=1;assert(budget.reserveDynamic(lists));
 for(int i=0;i<4000;++i)lists.add(JsonObject());assert(!budget.reserveFixed(d.as<JsonVariantConst>()));
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"notice.cpp";binary=Path(directory)/"notice";cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-Wno-misleading-indentation","-Wno-deprecated-declarations","-DCROSSPOINT_EMULATED=1","-I",str(JSON),"-I",str(SIM/"src"),"-I",str(ROOT/"src"),"-I",str(ROOT/"lib/Memory"),str(cpp),"-lcrypto","-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Actual notice ID/marker, Continue/cancel/OOM, SD failures, shared scope/update/reinstall, uninstall validation and list allocation admission pass")
