"""Real SDK three-pass ranking with installed-version and heap failure seams."""
from pathlib import Path
import re
import sys
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
JSON=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/"build/test/_deps/arduinojson-src/src"
def method(source,name):
    start=source.index(name);end=source.index("{",start)+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]
source=(ROOT/"src/activities/plugins/PluginCatalogActivity.cpp").read_text()
header=(ROOT/"src/activities/plugins/PluginCatalogActivity.h").read_text()
locations=(ROOT/"src/util/PluginLocations.cpp").read_text()
focus=method(source,'  if (result == HttpDownloader::OK && !selectedId.empty())')
assert source.count('return computeInstallStatus();')==2
assert 'if (!computeInstallStatus()) return true;' in source
code=r"""
#include <ArduinoJson.h>
#include <JsonListParser.h>
#include <Memory.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "util/PluginLocations.h"
unsigned errors=0;
#define LOG_ERR(...) (++errors)
constexpr size_t MAX_MANIFEST_SIZE=8*1024,MAX_CATALOG_BYTES=48*1024,CATALOG_HEAP_RESERVE=32*1024;
constexpr int MAX_PAGE_SIZE=16;
constexpr const char* BROWSE_TMP_PATH="/.pcat_tmp.json";
struct{size_t free=1000000,largest=1000000;size_t getFreeHeap()const{return free;}size_t getMaxAllocHeap()const{return largest;}}ESP;
struct{bool pluginsEnabled=true;}SETTINGS;
struct HalFile;
struct StorageFixture{
 std::map<std::string,std::string>files;std::string feed;
 int passes=0,versionReads=0;
 std::function<void(int)>beforePass,afterPass;
 bool exists(const char*p)const{const std::string prefix=std::string(p)+"/";return files.contains(p)||std::any_of(files.begin(),files.end(),[&](const auto&e){return e.first.starts_with(prefix);});}
 bool readFileToString(const char*,const std::string&p,size_t max,std::string&out){++versionReads;if(!files.contains(p)||files[p].size()>max)return false;out=files[p];return true;}
 bool openFileForRead(const char*,const char*,HalFile&);
}Storage;
struct HalFile{size_t pos=0;int pass=0;bool ended=false;
 int read(char*b,size_t n){if(pos==Storage.feed.size()){if(!ended&&Storage.afterPass)Storage.afterPass(pass);ended=true;return 0;}n=std::min(n,Storage.feed.size()-pos);memcpy(b,Storage.feed.data()+pos,n);pos+=n;return n;}};
bool StorageFixture::openFileForRead(const char*,const char*p,HalFile&f){assert(std::string(p)==BROWSE_TMP_PATH);f.pass=++passes;if(beforePass)beforePass(passes);return true;}
namespace PluginLocations {
"""+"\n".join(method(locations,n) for n in ['std::string findPluginDirOnDisk(','bool isPluginQuarantined(','std::string findPluginDir('])+r"""
}
namespace FsHelpers {
"""+method((ROOT/"lib/FsHelpers/FsHelpers.cpp").read_text(),'bool isValidPathComponent(')+r"""
}
"""+method(source,'bool catalogHasSpace(')+"\n"+re.search(r'enum class CatalogUpdateStatus[^;]+;',source).group()+"\n"+method(source,'CatalogUpdateStatus catalogItemUpdate(')+"\n"+method(source,'template <typename Parser>\nbool streamBrowseFile(')+r"""
int allocations=0,failAllocation=0;
#define makeUniqueNoThrow rankMakeUniqueNoThrow
template<class T,class...A>auto rankMakeUniqueNoThrow(A&&...a){if(++allocations==failAllocation)return std::unique_ptr<T>{};return std::make_unique<T>(std::forward<A>(a)...);}
constexpr const char*STR_INSTALLED="Installed",*STR_UPDATE_AVAILABLE="Update";
const char*tr(const char*s){return s;}
enum class StrId{STR_PARSE_FEED_FAILED};
namespace HttpDownloader{enum Result{OK,FAILED};}
struct PluginCatalogActivity{
"""+method(header,'  struct Item {')+r""";
 struct Manifest{
  std::string itemsPath="items",titlePath="title",authorPath="author",idPath="id",urlPath="url",versionPath="version",bundleBasePath="base",bundleFilesPath="files";
  std::string compatFamilyPath="family",compatRequiresPath="requires",compatApiMinPath="api";
  int pageSize=4;bool filter=false,tracks=true;
  bool filtersCompatibility()const{return filter;}bool tracksInstalls()const{return tracks;}
 }manifest;
 std::vector<Item>items;bool hasMore=false,failed=false;int page=1,releases=0;
 struct Nav{int chosen=-1,calls=0;void requestSelection(int i){chosen=i;++calls;}}nav;
 void releaseRows(){++releases;}
 void fail(StrId){failed=true;}
 bool catalogEntryCompatible(const std::string&family,const std::string&,const std::string&){return family!="exclude";}
 bool parseUpdateFirstBrowseResponse(size_t);bool computeInstallStatus();
 bool isXmlList=false;
 bool restoreFocus(HttpDownloader::Result result,const std::string&selectedId){
  struct ManifestXml{bool value;bool isXmlList()const{return value;}};
  const ManifestXml manifest{isXmlList};
"""+focus+r"""
 return true;}
};
"""+method(source,'bool PluginCatalogActivity::parseUpdateFirstBrowseResponse(')+"\n"+method(source,'bool PluginCatalogActivity::computeInstallStatus()')+r"""
void reset(){Storage={};ESP={};SETTINGS={};errors=0;allocations=failAllocation=0;}
void feed(int count,bool large=false){
 JsonDocument d;auto a=d["items"].to<JsonArray>();
 for(int i=0;i<count;++i){auto row=a.add<JsonObject>();row["title"]="Row"+std::to_string(i);row["id"]="p"+std::to_string(i);row["version"]=(i==2||i==20||i==35)?"2.0.0":"1.0.0";
  row["family"]="ok";Storage.files["/plugins/p"+std::to_string(i)+"/manifest.json"]="{\"version\":\"1.0.0\"}";
  if(large){row["author"]=std::string(700,'a');row["url"]=std::string(700,'u');row["base"]=std::string(700,'b');auto fs=row["files"].to<JsonArray>();for(int f=0;f<5;++f)fs.add(std::string(200,'f'));}
 }
 serializeJson(d,Storage.feed);
}
int main(){
 reset();feed(40);PluginCatalogActivity p;
 assert(p.parseUpdateFirstBrowseResponse(0)&&Storage.passes==3&&p.items.size()==4&&p.hasMore);
 assert(p.items[0].id=="p2"&&p.items[1].id=="p20"&&p.items[2].id=="p35"&&p.items[3].id=="p0");
 assert(p.items[0].status=="Update"&&p.items[3].status=="Installed");
 assert(p.parseUpdateFirstBrowseResponse(4)&&p.items[0].id=="p1"&&p.items[3].id=="p5");
 assert(p.parseUpdateFirstBrowseResponse(36)&&p.items.size()==4&&!p.hasMore);
 assert(p.parseUpdateFirstBrowseResponse(40)&&p.items.empty()&&!p.hasMore);
 reset();feed(40);SETTINGS.pluginsEnabled=false;PluginCatalogActivity off;assert(off.parseUpdateFirstBrowseResponse(0)&&off.items[0].id=="p0"&&Storage.versionReads==0);
 reset();feed(40);Storage.files["/.crosspoint/plugins/p2/manifest.json"]="{\"version\":\"9.0.0\"}";PluginCatalogActivity roots;assert(roots.parseUpdateFirstBrowseResponse(0)&&roots.items[0].id=="p20");
 reset();feed(40);Storage.feed.replace(Storage.feed.find("\"ok\""),4,"\"exclude\"");PluginCatalogActivity filtered;filtered.manifest.filter=true;assert(filtered.parseUpdateFirstBrowseResponse(0)&&filtered.items[3].id=="p1");
 reset();Storage.files["/plugins/demo/manifest.json"]="{\"version\":\"2.0.0\"}";
 assert(catalogItemUpdate("demo","1.0.0")==CatalogUpdateStatus::Installed);
 Storage.files["/plugins/demo/manifest.json"]="broken";assert(catalogItemUpdate("demo","3.0.0")==CatalogUpdateStatus::Available);
 int reads=Storage.versionReads;for(const char*id:{"../demo","demo/other","demo.","demo ",".",".."})assert(catalogItemUpdate(id,"2.0.0")==CatalogUpdateStatus::NotInstalled);
 assert(Storage.versionReads==reads);
 for(int allocation=1;allocation<=3;++allocation){reset();feed(4);failAllocation=allocation;PluginCatalogActivity a;assert(!a.parseUpdateFirstBrowseResponse(0)&&a.failed);}
 for(int mode=0;mode<5;++mode){
  reset();feed(4);
  if(mode==0)ESP.free=32768+MAX_MANIFEST_SIZE*3-1;
  if(mode==1)ESP.largest=4096+MAX_MANIFEST_SIZE*3-1;
  if(mode==2)Storage.afterPass=[](int n){if(n==2)ESP.free=32768;};
  if(mode==3)Storage.beforePass=[](int n){if(n==3)ESP.free=32768+4*sizeof(PluginCatalogActivity::Item);};
  if(mode==4)Storage.afterPass=[](int n){if(n==3)ESP.free=32768;};
  PluginCatalogActivity a;assert(!a.parseUpdateFirstBrowseResponse(0)&&a.failed);
 }
 reset();feed(16,true);PluginCatalogActivity capped;capped.manifest.pageSize=16;assert(!capped.parseUpdateFirstBrowseResponse(0)&&capped.failed);
 reset();feed(40);Storage.beforePass=[](int n){if(n==2)Storage.files["/plugins/p20/manifest.json"]="{\"version\":\"9.0.0\"}";};PluginCatalogActivity changed;assert(!changed.parseUpdateFirstBrowseResponse(12)&&changed.failed);
 reset();Storage.feed="{\"items\":[";PluginCatalogActivity malformed;assert(!malformed.parseUpdateFirstBrowseResponse(0)&&malformed.failed);
 reset();feed(4);PluginCatalogActivity focusCase;assert(focusCase.parseUpdateFirstBrowseResponse(0));focusCase.page=2;focusCase.restoreFocus(HttpDownloader::OK,"p0");assert(focusCase.nav.chosen==2&&focusCase.nav.calls==1);
 focusCase.items[1].id="same";focusCase.items[2].id="same";focusCase.restoreFocus(HttpDownloader::OK,"same");focusCase.restoreFocus(HttpDownloader::FAILED,"p0");focusCase.restoreFocus(HttpDownloader::OK,"");assert(focusCase.nav.calls==1);
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"ranking.cpp";binary=Path(directory)/"ranking";cpp.write_text(code)
    sdk=ROOT/"freeink-sdk/libs/network"
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-I",str(JSON),"-I",str(ROOT/"lib/Memory"),"-I",str(ROOT/"src"),"-I",str(sdk/"CatalogList/include"),"-I",str(sdk/"JsonSax/include"),str(cpp),str(sdk/"CatalogList/src/JsonListParser.cpp"),str(sdk/"JsonSax/src/StreamingJsonParser.cpp"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Real SDK three-pass ordering, cross-page stability/filtering, roots/IDs, allocation/heap/48KiB failures, changed rankings and unique-ID focus pass")
