"""Compile actual OPDS transport and acquisition methods against HTTP/SD/heap faults."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
SDK=ROOT/"freeink-sdk/libs/network"
def method(source, signature):
    start=source.index(signature);end=source.index("{",start)+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]

STUB_HTTP=r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <utility>
class HttpDownloader {
 public:
 enum DownloadError {OK,HTTP_ERROR,FILE_ERROR,ABORTED,UNAUTHORIZED};
 using Header=std::pair<std::string,std::string>;
 using DataCallback=std::function<bool(const uint8_t*,size_t)>;
 struct FetchOptions {std::string username,password,bearer,accept,acceptLanguage;std::vector<Header> headers;int* statusOut=nullptr;bool captureErrorBody=false;};
 static constexpr uint32_t MIN_TLS_FREE_HEAP=40000;
 static constexpr uint32_t MIN_TLS_MAX_ALLOC=20000;
 inline static bool tlsOk=true;inline static int gets=0,posts=0;
 inline static size_t licenseBytes=128;
 static FetchOptions last;
 inline static std::string lastPost;
 inline static std::string lastDownloadUser;
 inline static std::vector<Header> lastDownloadHeaders;
 static bool hasMemoryForTls(){return tlsOk;}
 static bool fetchUrl(const std::string&,const DataCallback& sink,const FetchOptions& options) {
  ++gets;last=options;if(options.statusOut)*options.statusOut=200;
  const std::string body(licenseBytes,'x');
  for(size_t pos=0;pos<body.size();pos+=1024)
   if(!sink(reinterpret_cast<const uint8_t*>(body.data()+pos),std::min<size_t>(1024,body.size()-pos)))return false;
  return true;
 }
 static bool postForm(const std::string&,const std::string& body,std::string& response,int* status) {
  ++posts;lastPost=body;response="wrapped";if(status)*status=200;return true;
 }
 static DownloadError downloadToFile(const std::string&,const std::string&,
   std::nullptr_t,std::nullptr_t,const std::string& user,const std::string&,const std::vector<Header>& headers) {
  lastDownloadUser=user;lastDownloadHeaders=headers;return OK;
 }
};
inline HttpDownloader::FetchOptions HttpDownloader::last;
'''
HARNESS=r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "Memory.h"
#include "network/HttpDownloader.h"
#include "network/OpdsHttpTransport.h"
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
unsigned long millis(){return 7;}
struct HalFile {
 const std::string* bytes=nullptr;
 bool isOpen() const{return bytes;}
 void close(){bytes=nullptr;}
 size_t read(void* out,size_t n){if(!bytes)return 0;n=std::min(n,bytes->size());memcpy(out,bytes->data(),n);return n;}
};
struct {
 std::map<std::string,std::string> files;
 bool replaceOk=true;
 bool exists(const char* p) const{return files.contains(p);}
 bool mkdir(const char* p){files[p]="";return true;}
 bool remove(const char* p){return files.erase(p)>0;}
 bool openFileForRead(const char*,const char* p,HalFile& out) {
  const auto i=files.find(p);if(i==files.end())return false;out.bytes=&i->second;return true;
 }
 bool replaceFile(const char* from,const char* to) {
  if(!replaceOk||!files.contains(from))return false;
  files[to]=files[from];files.erase(from);return true;
 }
} Storage;
struct HalMemory {
 struct HeapStats {size_t freeBytes,largestBlockBytes;};
 inline static HeapStats heap{1000000,1000000};
 static HeapStats getDefaultHeap(){return heap;}
};
struct {unsigned getFreeHeap(){return 1000000;}} ESP;
namespace UrlUtils {std::string buildUrl(const std::string& base,const std::string& ref){return ref.find("://")!=std::string::npos?ref:base+ref;}}
struct OpdsEntry {std::string title="book",href="/book.epub";bool lcpLicense=false,indirect=false;};
namespace freeink::content {
 struct LcpLicense {std::string provider="provider";};
 inline constexpr char LCP_FULFILL_URL[]="https://fulfill.test/";
 inline constexpr char LCP_UNLOCK_URL[]="https://unlock.test/";
 bool parseLcpLicense(const char*,size_t,LcpLicense*){return true;}
}
struct {bool enrolled=false;bool valid() const{return enrolled;}} LCP_DEVICE;
namespace lcpdevice {
 void loadOnce(){}
 bool enrollmentAvailable();
 bool ensureEnrolled(std::string& id,bool& notEnrollable){id="device";notEnrollable=false;return true;}
 bool unwrapContentKey(const std::string&,const std::string&,const std::string&,const std::string&,uint8_t*){return true;}
 void forget(){}
}
#define FREEINK_LCP_RELEASE_KEY ""
namespace bookkey {bool write(const std::string&,const uint8_t*,size_t,int64_t){return true;}}
int invalidations=0,registrations=0;
std::string cachePath;
void clearBookCache(const std::string& path){++invalidations;cachePath=path;}
namespace library {void markLibraryIndexDirty(){++registrations;}}
enum class StrId {STR_DOWNLOAD_FAILED,STR_OPDS_NOT_A_BOOK,STR_LCP_OFFICIAL_BUILD,STR_MEMORY_ERROR,STR_FETCH_FEED_FAILED,STR_LOADING,STR_LCP_UNSUPPORTED};
constexpr auto STR_LOADING=StrId::STR_LOADING;
const char* tr(StrId){return "";}
struct Client {
 freeink::opds::HttpAuth downloadAuth() const{return {"u","p","token",{{"X-Secret","secret"}}};}
 bool resolveIndirect(const std::string&,std::string&,bool&){return false;}
};
struct OpdsBookBrowserActivity {
 std::string statusMessage;
 struct {std::string url="https://catalog.test";} server;
 std::string currentPath,pendingLcpLicenseText,pendingLcpPath,detailCoverPath;
 freeink::content::LcpLicense pendingLcpLicense;
 OpdsHttpTransport opdsTransport;
 Client opdsClient;
 enum class LcpUnlock {Ok,WrongPassphrase,Failed};
 struct LcpWrappedKey {std::string epk,iv,ct,tag;};
 int releases=0,downloads=0,unlocks=0;
 StrId error=StrId::STR_LOADING;
 HttpDownloader::DownloadError result=HttpDownloader::OK,finished=HttpDownloader::HTTP_ERROR;
 std::string incoming="PK\3\4book";
 struct Renderer {void* getFontCacheManager(){return nullptr;}} renderer;
 void beginDownload(const std::string&) {}
 void releaseEntries(){++releases;}
 void fail(StrId value){error=value;}
 void requestUpdate(bool){}
 std::string downloadDestination(const OpdsEntry&) const{return "/Books/existing.epub";}
 bool verifyAndRegisterEpub(const std::string&,const std::string&);
 void downloadBook(const OpdsEntry&);
 void downloadLcpBook(const OpdsEntry&);
 void loadDetailCover(const std::string&,const std::string&);
 LcpUnlock requestLcpUnlock(const std::string&);
 void startLcpUnlock(){++unlocks;}
 void finishDownload(HttpDownloader::DownloadError value){finished=value;}
 static bool parseUnlockResponse(const std::string&,LcpWrappedKey* out,int64_t*){out->epk="e";return true;}
 HttpDownloader::DownloadError downloadFile(const std::string&,const std::string& path,
   const std::string& user="",const std::string& password="",const std::vector<HttpDownloader::Header>& headers={},
   const std::string* body=nullptr,const char* contentType=nullptr) {
  (void)password;(void)body;(void)contentType;
  assert(user.empty() || user=="u");(void)headers;
  ++downloads;
  if(result==HttpDownloader::OK)Storage.files[path]=incoming;
  else Storage.files[path+".part"]="partial";
  return result;
 }
};
'''
CHECKS=r'''
void reset() {
 Storage.files={{"/.crosspoint",""},{"/Books/existing.epub","old book"}};
 Storage.replaceOk=true;invalidations=registrations=0;
 HttpDownloader::gets=HttpDownloader::posts=0;HttpDownloader::tlsOk=true;HttpDownloader::licenseBytes=128;
 HalMemory::heap={1000000,1000000};LCP_DEVICE.enrolled=false;
}
int main() {
 reset();
 OpdsHttpTransport transport;
 transport.setServerUrl("HTTPS://CATALOG.test/feed");
 freeink::opds::HttpAuth auth{"u","p","token",{{"X-Secret","secret"}}};
 auto filtered=transport.authFor("https://cdn.test/cover.jpg",auth);
 assert(filtered.username.empty()&&filtered.bearer.empty()&&filtered.headers.empty());
 assert(transport.authFor("https://catalog.test:443/page",auth).headers.size()==1);
 transport.get("https://cdn.test/feed",auth,"application/opds+json","zh",false,[](const uint8_t*,size_t){return true;});
 assert(HttpDownloader::last.username.empty()&&HttpDownloader::last.bearer.empty()&&HttpDownloader::last.headers.empty());
 transport.get("https://catalog.test/feed",auth,nullptr,nullptr,false,[](const uint8_t*,size_t){return true;});
 assert(HttpDownloader::last.username=="u"&&HttpDownloader::last.bearer=="token"&&HttpDownloader::last.headers.size()==1);
 std::string response;int status=0;
 transport.postForm("https://sso.test/token","explicit-password-body",response,status);
 assert(HttpDownloader::lastPost=="explicit-password-body");
 OpdsBookBrowserActivity cover;cover.opdsTransport.setServerUrl(cover.server.url);
 cover.loadDetailCover("https://catalog.test/feed","https://cdn.test/cover.jpg");
 assert(HttpDownloader::lastDownloadUser.empty()&&HttpDownloader::lastDownloadHeaders.empty());
 cover.loadDetailCover("https://catalog.test/feed","https://catalog.test/cover.jpg");
 assert(HttpDownloader::lastDownloadUser=="u"&&HttpDownloader::lastDownloadHeaders.size()==2);
 reset();
 OpdsEntry book;
 OpdsBookBrowserActivity noKey;noKey.downloadLcpBook(book);
 assert(noKey.error==StrId::STR_LCP_OFFICIAL_BUILD&&HttpDownloader::gets==0&&noKey.downloads==0);
 reset();LCP_DEVICE.enrolled=true;HttpDownloader::tlsOk=false;
 OpdsBookBrowserActivity noTls;noTls.downloadLcpBook(book);
 assert(noTls.error==StrId::STR_MEMORY_ERROR&&HttpDownloader::gets==0);
 reset();LCP_DEVICE.enrolled=true;HttpDownloader::licenseBytes=256*1024;
 OpdsBookBrowserActivity large;large.opdsTransport.setServerUrl(large.server.url);large.downloadLcpBook(book);
#ifdef BOARD_HAS_PSRAM
 assert(large.downloads==1&&large.pendingLcpLicenseText.size()==256*1024);
#else
 assert(large.error==StrId::STR_MEMORY_ERROR&&large.downloads==0);
 assert(Storage.files["/Books/existing.epub"]=="old book");
#endif
 reset();LCP_DEVICE.enrolled=true;HalMemory::heap={45000,4096};
 OpdsBookBrowserActivity fragmented;fragmented.downloadLcpBook(book);
 assert(fragmented.error==StrId::STR_MEMORY_ERROR&&fragmented.downloads==0);
 reset();LCP_DEVICE.enrolled=true;
 OpdsBookBrowserActivity small;small.opdsTransport.setServerUrl(small.server.url);small.downloadLcpBook(book);
 assert(small.downloads==1&&small.unlocks==1&&small.pendingLcpLicenseText.size()==128);
 assert(small.releases>=1&&Storage.files["/Books/existing.epub"]=="PK\3\4book");
 HalMemory::heap={40000,1000000};HttpDownloader::posts=0;
 assert(small.requestLcpUnlock(std::string(64,'f'))==OpdsBookBrowserActivity::LcpUnlock::Failed);
 assert(small.error==StrId::STR_MEMORY_ERROR&&HttpDownloader::posts==0);
 HalMemory::heap={1000000,1000000};
 assert(small.requestLcpUnlock(std::string(64,'f'))==OpdsBookBrowserActivity::LcpUnlock::Ok);
 assert(HttpDownloader::posts==1);
 for(auto failure:{HttpDownloader::HTTP_ERROR,HttpDownloader::ABORTED,HttpDownloader::FILE_ERROR}) {
  reset();OpdsBookBrowserActivity r;r.opdsTransport.setServerUrl(r.server.url);r.result=failure;r.downloadBook(book);
  assert(Storage.files["/Books/existing.epub"]=="old book");
  for(const auto& [name,data]:Storage.files)assert(name.find(".download")==std::string::npos);
 }
 reset();OpdsBookBrowserActivity html;html.incoming="<html>login</html>";html.downloadBook(book);
 assert(html.error==StrId::STR_OPDS_NOT_A_BOOK&&Storage.files["/Books/existing.epub"]=="old book");
 assert(invalidations==0&&registrations==0);
 reset();Storage.replaceOk=false;OpdsBookBrowserActivity failed;failed.downloadBook(book);
 assert(failed.error==StrId::STR_DOWNLOAD_FAILED&&Storage.files["/Books/existing.epub"]=="old book");
 reset();OpdsBookBrowserActivity good;good.downloadBook(book);
 assert(Storage.files["/Books/existing.epub"]=="PK\3\4book"&&registrations==1&&invalidations==1);
 assert(cachePath=="/Books/existing.epub");
 for(const auto& [name,data]:Storage.files)assert(name.find(".download")==std::string::npos);
 puts("OPDS initial-origin credentials, license budgets/availability and verified acquisition transactions passed");
}
'''

class OpdsSafetyTest(unittest.TestCase):
    def test_production_acquisition_and_transport_internal_and_psram(self):
        source=(ROOT/"src/activities/browser/OpdsBookBrowserActivity.cpp").read_text()
        device=(ROOT/"src/network/LcpDevice.cpp").read_text()
        # The renderer seam models only cache release, never rendering or font ownership.
        harness=HARNESS.replace("struct Renderer {void* getFontCacheManager(){return nullptr;}} renderer;",
            "struct Cache {void releaseSdFontCaches(){}};struct Renderer {Cache cache;Cache* getFontCacheManager(){return &cache;}} renderer;")
        helpers=source[source.index("// License text and the unlock JSON"):source.index("// Accept-Language")]
        methods="\n".join(method(source,sig) for sig in [
            "bool OpdsBookBrowserActivity::verifyAndRegisterEpub(",
            "void OpdsBookBrowserActivity::downloadBook(",
            "void OpdsBookBrowserActivity::downloadLcpBook(",
            "void OpdsBookBrowserActivity::loadDetailCover(",
            "OpdsBookBrowserActivity::LcpUnlock OpdsBookBrowserActivity::requestLcpUnlock("])
        origin=(SDK/"SecureNet/include/ResumableFetch.h").read_text()
        with tempfile.TemporaryDirectory(prefix="opds-safety-") as directory:
            temp=Path(directory);(temp/"network").mkdir()
            (temp/"network/HttpDownloader.h").write_text(STUB_HTTP)
            (temp/"ResumableFetch.h").write_text("#pragma once\n#include <string>\n#include <cctype>\nnamespace freeink {\n"+method(origin,"inline std::string fetchOrigin(")+"\n}\n")
            cpp=temp/"check.cpp"
            cpp.write_text(harness+"\nnamespace lcpdevice {\n"+method(device,"bool enrollmentAvailable(")+"\n}\n"+helpers+methods+CHECKS)
            for name,flags in [("c3",[]),("psram",["-DBOARD_HAS_PSRAM"])]:
                binary=temp/name
                subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-Wno-unused-parameter",*flags,
                    "-I"+str(temp),"-I"+str(ROOT/"src"),"-I"+str(ROOT/"lib/Memory"),
                    "-I"+str(SDK/"Opds/include"),str(cpp),str(ROOT/"src/network/OpdsHttpTransport.cpp"),
                    "-o",str(binary)],check=True)
                subprocess.run([str(binary)],check=True)


if __name__=="__main__":
    unittest.main()
