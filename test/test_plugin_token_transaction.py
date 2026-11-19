"""Compile production token functions against storage failure injection and real ArduinoJson."""
from pathlib import Path
import os
import sys
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
JSON=Path(sys.argv[1]) if len(sys.argv)>1 else Path(os.environ.get("CROSSMUX_ARDUINOJSON_SOURCE",ROOT/"build/test/_deps/arduinojson-src/src"))

def method(source,name):
    start=source.index(name);end=source.index("{",start)+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]

source=(ROOT/"src/util/PluginHttp.cpp").read_text()
code=r"""
#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include <Memory.h>
#include <network/ProtectedPaths.h>
#define LOG_ERR(...) ((void)0)
constexpr size_t MAX_TOKEN_FILE_SIZE=2048;
struct HalMemory{
 struct Heap{size_t freeBytes,largestBlockBytes;};
 static inline Heap heap{1000000,1000000};
 static Heap getDefaultHeap(){return heap;}
};
struct HalFile;
struct HalStorage{
 struct StorageLock{~StorageLock(){}};
 std::map<std::string,std::string> files;
 std::set<std::pair<std::string,std::string>> renameFailures;
 bool openFailure=false,closeFailure=false;
 size_t writeBudget=SIZE_MAX;
 bool exists(const char* p){return files.contains(p);}
 bool remove(const char* p){return files.erase(p)!=0;}
 bool rename(const char* a,const char* b){
  if(renameFailures.contains({a,b})||!files.contains(a)||files.contains(b))return false;
  files[b]=std::move(files[a]);files.erase(a);return true;
 }
 bool readFileToString(const char*,const std::string& p,size_t max,std::string& out){
  if(!files.contains(p)||files[p].size()>max)return false;out=files[p];return true;
 }
 bool openFileForWrite(const char*,const std::string&,HalFile&);
} Storage;
struct HalFile{
 std::string path;
 bool close(){return !Storage.closeFailure;}
 size_t write(const uint8_t* bytes,size_t count){
  size_t actual=std::min(count,Storage.writeBudget);Storage.writeBudget-=actual;
  Storage.files[path].append(reinterpret_cast<const char*>(bytes),actual);return actual;
 }
 size_t write(uint8_t byte){return write(&byte,1);}
};
bool HalStorage::openFileForWrite(const char*,const std::string& path,HalFile& out){
 if(openFailure)return false;files[path].clear();out.path=path;return true;
}
"""+"\n".join(method(source,name) for name in [
"static bool segIsIndex(","void splitPath(","JsonVariantConst resolvePath(","std::string variantToString(",
"static bool allowedFile(","static bool tokenSiblingPaths(","static bool readTokenRecord(","static bool recoverTokenRecord(",
"bool loadTokenFromFile(","bool saveTokenToFile("])+r"""
const std::string target="/plugins/demo/token.json";
const std::string oldRecord="{\"auth\":{\"token\":\"old\"}}";
void reset(){Storage={};HalMemory::heap={1000000,1000000};Storage.files[target]=oldRecord;}
void expectOld(){std::string token;assert(loadTokenFromFile(target,"auth.token",token)&&token=="old");}
int main(){
 reset();assert(saveTokenToFile(target,"auth.token","new"));
 std::string token;assert(loadTokenFromFile(target,"auth.token",token)&&token=="new");
 assert(!Storage.files.contains(target+".bak")&&!Storage.files.contains(target+".tmp"));
 for(int fault:{0,1,2}){
  reset();Storage.openFailure=fault==0;Storage.writeBudget=fault==1?10:SIZE_MAX;Storage.closeFailure=fault==2;
  assert(!saveTokenToFile(target,"auth.token","replacement"));
  assert(Storage.files[target]==oldRecord);Storage.openFailure=Storage.closeFailure=false;Storage.writeBudget=SIZE_MAX;expectOld();
 }
 reset();Storage.renameFailures.insert({target,target+".bak"});
 assert(!saveTokenToFile(target,"auth.token","new"));expectOld();
 for(bool rollbackFails:{false,true}){
  reset();Storage.renameFailures.insert({target+".tmp",target});
  if(rollbackFails)Storage.renameFailures.insert({target+".bak",target});
  assert(!saveTokenToFile(target,"auth.token","new"));
  assert(Storage.files.contains(rollbackFails?target+".bak":target));
  Storage.renameFailures.clear();expectOld();
 }
 reset();Storage.files.clear();Storage.renameFailures.insert({target+".tmp",target});
 assert(!saveTokenToFile(target,"auth.token","new"));assert(Storage.files.contains(target+".tmp"));
 Storage.renameFailures.clear();assert(loadTokenFromFile(target,"auth.token",token)&&token=="new");
 for(const auto& bad:std::vector<std::string>{"notes","{}","{\"auth\":",std::string(2050,'x')}){
  reset();Storage.files[target]=bad;auto before=Storage.files;
  assert(!saveTokenToFile(target,"auth.token","new"));assert(Storage.files==before);
 }
 for(const auto& suffix:{".tmp",".bak"}){
  reset();Storage.files[target+suffix]="user notes";auto before=Storage.files;
  assert(!saveTokenToFile(target,"auth.token","new"));assert(Storage.files==before);expectOld();
 }
 reset();auto before=Storage.files;assert(!saveTokenToFile(target,"auth.token",std::string(2048,'x')));assert(Storage.files==before);
 reset();before=Storage.files;HalMemory::heap={8192,1000000};assert(!saveTokenToFile(target,"auth.token","new"));assert(Storage.files==before);
 reset();before=Storage.files;assert(!saveTokenToFile(target,"auth.0","new"));assert(Storage.files==before);
 reset();assert(!saveTokenToFile("/.crosspoint/wifi.json","token","new"));assert(Storage.files.size()==1);
 token="stale";assert(loadTokenFromFile("","token",token)&&token.empty());
 reset();assert(saveTokenToFile(target,"auth.token",""));assert(!loadTokenFromFile(target,"auth.token",token)&&token.empty());
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"token.cpp";binary=Path(directory)/"token"
    cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-Wno-misleading-indentation","-I",str(JSON),"-I",str(ROOT/"lib/Memory"),"-I",str(ROOT/"src"),str(cpp),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Production token open/short-write/close/backup/install/rollback failures, recovery, bounds and unknown-file preservation pass")
