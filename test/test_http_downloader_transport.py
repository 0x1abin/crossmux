"""Build the production downloader against deterministic HTTP/storage faults, without sockets."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STORAGE = r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
inline std::map<std::string,std::string> files;
inline bool openOk=true,writeOk=true,replaceOk=true;
struct HalFile {
 std::string path;bool opened=false;
 size_t write(const uint8_t* data,size_t size) { if(!writeOk)return 0;files[path].append(reinterpret_cast<const char*>(data),size);return size; }
 bool isOpen() const {return opened;}
 void close() {opened=false;}
};
struct {
 bool remove(const char* path) {files.erase(path);return true;}
 bool openFileForWrite(const char*,const char* path,HalFile& file) {if(!openOk)return false;file.path=path;file.opened=true;files[path].clear();return true;}
 bool replaceFile(const char* from,const char* to) {if(!replaceOk)return false;files[to]=files[from];files.erase(from);return true;}
} inline Storage;
'''
CLIENT = r'''
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace freeink {
using DataCallback=std::function<bool(const uint8_t*,size_t)>;
using AbortCallback=std::function<bool()>;
struct Response {int status=200;std::string body="book",location;bool complete=true,known=true;};
struct Request {std::string url,agent,method;std::map<std::string,std::string> headers;};
inline std::vector<Response> responses;
inline std::vector<Request> requests;
inline bool beginOk=true;
inline bool fetchAborted=false,fetchStopped=false,fetchComplete=true;
inline int fetchStatus=200;
inline bool downgraded=false;
struct SecureHttpClient {
 Request request;Response response;bool stopped=false,wasAborted=false;
 bool begin(const std::string& url) {request.url=url;return beginOk;}
 void setTimeout(int ms) {if(ms!=60000) std::abort();}
 void setInsecure() {}
 void setUserAgent(const std::string& value) {request.agent=value;}
 void setBasicAuth(const std::string& user,const std::string& pass) {request.headers["Authorization"]="Basic "+user+":"+pass;}
 void addHeader(const std::string& name,const std::string& value) {request.headers[name]=value;}
 static bool resolveUrl(const std::string& base,const std::string& location,std::string& out) {
  if(location.empty()) return false;
  if(location.find("://")!=std::string::npos) {out=location;return true;}
  const auto end=base.find('/',base.find("://")+3);out=base.substr(0,end)+location;return true;
 }
 int sendRequest(const char* method,const uint8_t*,size_t,const DataCallback& callback,const AbortCallback& abort={}) {
  request.method=method;requests.push_back(request);
  response=responses.empty()?Response{}:responses.front();if(!responses.empty())responses.erase(responses.begin());
  if(abort && abort()) {wasAborted=true;return -1;}
  if(!response.body.empty()&&!callback(reinterpret_cast<const uint8_t*>(response.body.data()),response.body.size()))stopped=true;
  return response.status;
 }
 int GET(const DataCallback& callback) {return sendRequest("GET",nullptr,0,callback);}
 int getStatus() const {return response.status;}
 std::string getHeader(const char*) const {return response.location;}
 bool responseComplete() const {return response.complete&&!stopped&&!wasAborted;}
 bool hasContentLength() const {return response.known;}
 size_t getContentLength() const {return response.body.size();}
 bool aborted() const {return wasAborted;}
};
struct FetchOptions {bool redirectToHttp=false;};
struct FetchSink {DataCallback write;std::function<bool()> rewind;std::function<void(size_t,size_t)> progress;};
struct FetchResult {int status=200;size_t bytes=0,total=0;bool complete=true,aborted=false,stopped=false;};
inline std::string fetchOrigin(const std::string& url) {return url.substr(0,url.find('/',url.find("://")+3));}
template<class Setup,class Abort>
FetchResult fetchResumable(const std::string& url,const FetchOptions& options,Setup setup,const FetchSink& sink,Abort abort) {
 downgraded=options.redirectToHttp;
 for(bool same : {true,false}) {SecureHttpClient c;c.begin(url);setup(c,same);requests.push_back(c.request);}
 FetchResult result;result.status=fetchStatus;result.complete=fetchComplete;result.aborted=fetchAborted||abort();result.stopped=fetchStopped;
 if(!result.aborted&&!result.stopped&&fetchStatus==200) {
  const uint8_t body[]={'b','o','o','k'};result.total=4;
  if(sink.write(body,4))result.bytes=4;else result.stopped=true;
 }
 return result;
}
}
'''
HARNESS = r'''
#include <cassert>
#include "network/HttpDownloader.h"
#include <ResumableFetch.h>
using namespace freeink;
void reset() {responses.clear();requests.clear();files.clear();beginOk=openOk=writeOk=replaceOk=true;fetchAborted=fetchStopped=false;fetchComplete=true;fetchStatus=200;}
int main() {
 reset();
 HttpDownloader::FetchOptions options;
 options.username="u";options.password="p";options.bearer="token";
 options.headers={{"X-Access","secret"},{"Bad Name","bad"},{"X-Injection","a\r\nX-Evil: yes"}};
 options.accept="application/opds+json";options.acceptLanguage="zh-CN";
 int status=0;options.statusOut=&status;
 responses={{302,"ignored","/same"},{307,"ignored","https://cdn.test/book"},{200,"book"}};
 std::string out;
 assert(HttpDownloader::fetchUrl("https://origin.test/feed",[&](const uint8_t* data,size_t n){out.append(reinterpret_cast<const char*>(data),n);return true;},options));
 assert(out=="book"&&status==200&&HttpDownloader::lastAnswerRedirected());
 assert(requests.size()==3);
 for(size_t i=0;i<3;++i) {
  assert(requests[i].agent=="CrossMux-HostTest-test");
  assert(requests[i].headers.count("Bad Name")==0&&requests[i].headers.count("X-Injection")==0);
  assert(requests[i].headers.count("Authorization")==static_cast<unsigned>(i<2));
  assert(requests[i].headers.count("X-Access")==static_cast<unsigned>(i<2));
 }
 assert(requests[0].headers["Authorization"]=="Bearer token");
 reset();responses={{302,"ignored","http://origin.test/insecure"}};
 assert(!HttpDownloader::fetchUrl("https://origin.test/feed",[](const uint8_t*,size_t){return true;},options));
 assert(requests.size()==1&&status==302&&!HttpDownloader::lastAnswerRedirected());
 reset();responses={{401,"auth-document"}};out.clear();options.captureErrorBody=true;
 assert(!HttpDownloader::fetchUrl("https://origin.test/feed",[&](const uint8_t* d,size_t n){out.append(reinterpret_cast<const char*>(d),n);return true;},options));
 assert(status==401&&out=="auth-document");
 reset();responses={{200,"partial","",false}};
 assert(!HttpDownloader::fetchUrl("https://origin.test/feed",[](const uint8_t*,size_t){return true;},options));
 reset();responses={{200,"book"}};
 assert(!HttpDownloader::fetchUrl("https://origin.test/feed",[](const uint8_t*,size_t){return false;},options));
 reset();responses={{200,std::string(10000,'x')}};
 assert(HttpDownloader::postForm("https://origin.test/token","grant_type=password",out,&status));
 assert(out.size()==4096&&status==200&&requests.back().agent=="CrossMux-HostTest-test");
 reset();responses={{200,"partial","",false}};
 assert(!HttpDownloader::postForm("https://origin.test/token","",out));
 for(int failure=0;failure<5;++failure) {
  reset();files["book.epub"]="old";
  if(failure==0)beginOk=false;
  if(failure==1)responses={{200,"partial","",false}};
  if(failure==2)writeOk=false;
  if(failure==3)replaceOk=false;
  bool cancel=failure==4;
  assert(HttpDownloader::postToFile("https://origin.test/loan","body","application/json","book.epub",{},&cancel)!=HttpDownloader::OK);
  assert(files["book.epub"]=="old"&&files.count("book.epub.part")==0);
 }
 reset();assert(HttpDownloader::postToFile("https://origin.test/loan","body","application/json","book.epub")==HttpDownloader::OK);
 assert(files["book.epub"]=="book"&&files.count("book.epub.part")==0);
 reset();
 const std::vector<HttpDownloader::Header> headers={{"X-Access","secret"},{"Bad Name","bad"},{"X-Injection","a\nX-Evil: yes"}};
 assert(HttpDownloader::downloadToFile("https://origin.test/book","book.epub",{},nullptr,"u","p",headers,true)==HttpDownloader::OK);
 assert(downgraded&&files["book.epub"]=="book");
 assert(requests[0].headers.count("Authorization")==1&&requests[0].headers.count("X-Access")==1);
 assert(requests[0].headers.count("Bad Name")==0&&requests[0].headers.count("X-Injection")==0);
 assert(requests[1].headers.empty());
 reset();fetchComplete=false;files["book.epub"]="old";
 assert(HttpDownloader::downloadToFile("https://origin.test/book","book.epub")!=HttpDownloader::OK);
 assert(files["book.epub"]=="old"&&files.count("book.epub.part")==0);
}
'''


def run():
    with tempfile.TemporaryDirectory(prefix='http-downloader-') as tmp:
        tmp=Path(tmp)
        (tmp/'HalStorage.h').write_text(STORAGE)
        (tmp/'ResumableFetch.h').write_text(CLIENT)
        (tmp/'Arduino.h').write_text('#pragma once\n#include <cstdio>\n#include <cstdint>\n#include <cstddef>\nclass Stream {public: virtual size_t write(const uint8_t*,size_t)=0;};\n')
        (tmp/'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n#define LOG_DBG(...) ((void)0)\n')
        (tmp/'HalMemory.h').write_text('#pragma once\n#include <cstddef>\nnamespace HalMemory {struct HeapStats {size_t freeBytes=100000,largestBlockBytes=100000;};inline HeapStats getDefaultHeap(){return{};}inline HeapStats getInternalHeap(){return{};}inline HeapStats getPsramHeap(){return{};}}\n')
        (tmp/'HalSystem.h').write_text('#pragma once\nnamespace HalSystem {inline const char* getDeviceModel(){return "HostTest";}}\n')
        (tmp/'esp_wifi.h').write_text('#pragma once\nusing wifi_ps_type_t=int;using esp_err_t=int;constexpr int ESP_OK=0,WIFI_PS_MIN_MODEM=1,WIFI_PS_NONE=0;inline int esp_wifi_get_ps(int* out){*out=1;return 0;}inline int esp_wifi_set_ps(int){return 0;}\n')
        (tmp/'check.cpp').write_text(HARNESS)
        binary=tmp/'check'
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-DCROSSPOINT_VERSION="test"',
                        '-I',str(tmp),'-I',str(ROOT/'src'),'-I',str(ROOT/'lib/HttpHeader'),'-I',str(ROOT/'lib/AsciiText'),
                        str(tmp/'check.cpp'),str(ROOT/'src/network/HttpDownloader.cpp'),str(ROOT/'lib/HttpHeader/HttpHeader.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    print('HTTP downloader: origin-scoped Basic/Bearer/custom headers, CRLF rejection, 401 body, interrupted200, capped token response, cancellation and atomic files passed')


if __name__ == '__main__':
    run()
