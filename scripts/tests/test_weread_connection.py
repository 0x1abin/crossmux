"""Exercise production WeRead transport, authentication and memory ownership."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from test_reading_ui_regressions import method

ROOT = Path(__file__).resolve().parents[2]
API = ROOT / "lib/WeReadWebApi/src"
ACTIVITY = ROOT / "src/activities/apps/weread/webapi/WeReadActivity.cpp"
SAX = ROOT / "freeink-sdk/libs/network/JsonSax"


def run(program, headers=None, sources=(), defines=()):
    with tempfile.TemporaryDirectory(prefix="weread-connection-") as directory:
        directory = Path(directory)
        for name, content in (headers or {}).items():
            (directory / name).write_text(content)
        cpp = directory / "check.cpp"
        cpp.write_text(program)
        binary = directory / "check"
        subprocess.run([
            "c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-Wno-class-memaccess",
            "-I" + str(directory), "-I" + str(API), "-I" + str(SAX / "include"),
            *["-D" + value for value in defines], str(cpp), *map(str, sources), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


ARDUINO = r'''
#pragma once
#include <cstddef>
#include <cstdint>
inline unsigned long ticks = 100;
inline unsigned long millis() { return ticks; }
inline void delay(unsigned long value) { ticks += value; }
inline int uxTaskGetStackHighWaterMark(void*) { return 4096; }
[[maybe_unused]] inline struct { unsigned getFreeHeap() { return 64000; } unsigned getMaxAllocHeap() { return 32000; } } ESP;
'''

SECURE = r'''
#pragma once
#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
namespace fixture {
enum class Phase { Idle, Handshake, Write, Read, Body };
inline Phase phase = Phase::Idle, cancelAt = Phase::Idle;
inline std::string response;
inline bool holdHandshake = false, callbackHeld = false, oneShot = false;
inline size_t position = 0;
inline int cancellations = 0;
inline bool cancel(void*) {
  if (cancelAt == Phase::Idle || phase != cancelAt) return false;
  ++cancellations;
  if (oneShot) cancelAt = Phase::Idle;
  return true;
}
}
namespace freeink {
class SecureClient {
  bool connected_ = false;
  std::function<bool()> abort_;
 public:
  void stop() { connected_ = false; }
  bool connected() { return connected_; }
  void setInsecure() {}
  void setTimeout(unsigned long) {}
  void setAbortCallback(std::function<bool()> callback) { abort_ = callback; fixture::callbackHeld = bool(callback); }
  int connect(const char*, uint16_t) {
    fixture::phase = fixture::Phase::Handshake;
    if (fixture::holdHandshake && abort_ && abort_()) return 0;
    fixture::position = 0;
    connected_ = true;
    return 1;
  }
  size_t write(const uint8_t*, size_t length) { fixture::phase = fixture::Phase::Write; return length; }
  int read() {
    fixture::phase = fixture::Phase::Read;
    if (fixture::position == fixture::response.size()) return -1;
    return static_cast<unsigned char>(fixture::response[fixture::position++]);
  }
  int read(uint8_t* out, size_t length) {
    fixture::phase = fixture::Phase::Body;
    size_t count = std::min(length, fixture::response.size() - fixture::position);
    memcpy(out, fixture::response.data() + fixture::position, count);
    fixture::position += count;
    return static_cast<int>(count);
  }
  int available() { return static_cast<int>(fixture::response.size() - fixture::position); }
};
}
'''


class WeReadConnectionTest(unittest.TestCase):
    def test_shelf_http_and_json_authentication_failures(self):
        source = (API / "WeReadClient.cpp").read_text()
        http_header = (API / "WeReadHttpClient.h").read_text()
        store_header = (API / "WeReadStore.h").read_text()
        client_header = (API / "WeReadClient.h").read_text()
        program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include "StreamingJsonParser.h"
#define LOG_ERR(...) ((void)0)
template<class... Args> void logInfo(Args&&...) {}
#define LOG_INF(...) logInfo(__VA_ARGS__)
unsigned long millis() { return 100; }
void logMemory(const char*) {}
constexpr int kRequestTimeoutMs=20000;
constexpr const char* kUserAgent="test", *kOrigin="https://weread.qq.com", *kHost=kOrigin, *kDefaultReferer=kOrigin;
template<class T, class... Args> auto makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}
namespace fixture { int status=200, persistedBooks=7, dataCalls=0, accountClears=0; bool networkFailure=false; std::string body; }
namespace WeReadHttpClient {
using AbortCallback=bool (*)(void*);
using DataCallback=std::function<bool(const uint8_t*, size_t)>;
using HeaderCallback=std::function<void(const char*, const char*)>;
struct Header { const char* name; const char* value; };
struct Session {};
''' + method(http_header, "enum class Result") + ";\n" + method(http_header, "struct RequestOptions") + r''';
Result request(const char*, const RequestOptions& options, const DataCallback& onData, const HeaderCallback&, int& status) {
  status=-1;
  if (fixture::networkFailure) return Result::NetworkError;
  status=fixture::status;
  if (options.acceptStatus && !options.acceptStatus(status)) return Result::Aborted;
  for (size_t i=0; i<fixture::body.size(); i+=2) {
    ++fixture::dataCalls;
    if (!onData(reinterpret_cast<const uint8_t*>(fixture::body.data()+i), std::min<size_t>(2, fixture::body.size()-i)))
      return Result::Aborted;
  }
  return Result::Ok;
}
Result request(Session&, const char* url, const RequestOptions& options, const DataCallback& onData,
               const HeaderCallback& onHeader, int& status) { return request(url, options, onData, onHeader, status); }
}
namespace WeReadStore {
''' + method(store_header, "struct Session") + ";\n" + method(store_header, "struct ShelfRecord") + r''';
void Session::clear() { memset(this, 0, sizeof(*this)); }
bool Session::setCookie(const char* name, const char* value, size_t length) {
  char* field = strcmp(name, "wr_vid") == 0 ? vid : skey;
  memcpy(field, value, length); field[length]=0; return true;
}
constexpr const char* kShelfPath="shelf.bin"; constexpr uint32_t kShelfMagic=123;
struct IndexWriter {
  uint32_t count_=0;
  bool begin(const char*, uint32_t, size_t) { count_=0; return true; }
  void abort() {} bool append(const void*) { ++count_; return true; }
  uint32_t count() { return count_; }
  bool finish() { fixture::persistedBooks=count_; return true; }
};
bool saveSession(const Session&) { return true; }
bool clearShelf() { ++fixture::accountClears; return true; }
}
namespace WeReadBrowse { bool clearAllCaches() { ++fixture::accountClears; return true; } }
namespace WeReadProtocol {
enum class ImageType { None };
ImageType normalizeCoverImageUrl(const char*, char*, size_t) { return ImageType::None; }
uint32_t parseUint32OrZero(const char* value, size_t) { return strtoul(value, nullptr, 10); }
}
void copyDecoded(const char* value, size_t len, char* dest, size_t capacity) {
  len=std::min(len, capacity-1); memcpy(dest, value, len); dest[len]=0;
}
bool mergeSessionCookies(WeReadStore::Session*, char*, size_t) { return true; }
bool absorbSetCookie(WeReadStore::Session*, const char*, const char*, char*, size_t) { return true; }
bool noOpFinish(void*) { return true; }
''' + method(client_header, "enum class Error") + ";\n" + method(source, "struct ResponseSink") + ";\n"
        for name in ("bool acceptAuthenticatedStatus", "bool acceptChapterStatus", "Error requestOnce"):
            program += method(source, name) + "\n"
        simple = source[source.index("enum class SimpleField"):source.index("bool resetRemoteProgress")]
        program += simple.replace(method(source, "void copyDecoded"), "")
        program += source[source.index("enum class ShelfField"):source.index("enum class TocField")]
        program += r'''
struct Operation {
 enum class Kind { Sync,Browse }; enum class Phase { SyncShelf,PrepareBrowseCache };
 Kind kind_=Kind::Sync; Phase resumePhase_=Phase::SyncShelf;
 WeReadStore::Session session_; int responseStatus_=0;
 char cookie_[896]{}, url_[512]{}; uint8_t ioBuffer_[4096]{};
 char loginUid_[128]{}, previousVid_[64]{};
 bool loginConfirmed_=false, browseCacheActive_=false; int browseManifest_=0;
 uint32_t progressCompleted_=0, progressTotal_=0;
 static bool checkCancellation(void*) { return false; }
 Error syncShelfOnce(); Error fetchLoginUid(); Error pollLogin();
};
'''
        for name in ("Error Operation::syncShelfOnce", "Error Operation::fetchLoginUid", "Error Operation::pollLogin"):
            program += method(source, name) + "\n"
        program += r'''
int main() {
  for (int status : {401, 403}) {
    for (const char* body : {"", "<html>denied</html>", "{\"errcode\":-2012}"}) {
      fixture::status=status; fixture::body=body; fixture::dataCalls=0;
      Operation op;
      assert(op.syncShelfOnce() == Error::SessionExpired);
      assert(fixture::dataCalls == 0 && fixture::persistedBooks == 7);
    }
  }
  fixture::status=200;
  for (const char* body : {"{\"errcode\":-2012}", "{\"errCode\":-2012}"}) {
    fixture::body=body; Operation op;
    assert(op.syncShelfOnce() == Error::SessionExpired && fixture::persistedBooks == 7);
  }
  fixture::networkFailure=true; Operation network;
  assert(network.syncShelfOnce() == Error::Network && fixture::persistedBooks == 7);
  fixture::networkFailure=false;
  fixture::body="{\"books\":[{\"bookId\":\"123\",\"title\":\"book\"}]}";
  Operation normal; assert(normal.syncShelfOnce() == Error::Ok);
  assert(fixture::persistedBooks == 1 && normal.progressCompleted_ == 1);
  assert(!acceptAuthenticatedStatus(403) && acceptChapterStatus(403) && !acceptChapterStatus(401));
  fixture::body="{\"errcode\":-2012}";
  Operation expired; assert(expired.fetchLoginUid() == Error::SessionExpired);
  assert(expired.pollLogin() == Error::SessionExpired);
  Operation login; strcpy(login.previousVid_, "account");
  fixture::body="{\"uid\":\"challenge\"}";
  assert(login.fetchLoginUid() == Error::Ok && std::string(login.loginUid_) == "challenge");
  fixture::body="{\"succeed\":true,\"webLoginVid\":\"account\",\"accessToken\":\"new-key\"}";
  assert(login.pollLogin() == Error::Ok && login.loginConfirmed_);
  assert(login.session_.valid() && fixture::accountClears == 0);
  assert(login.resumePhase_ == Operation::Phase::SyncShelf);
  fixture::body="{\"succeed\":true,\"webLoginVid\":\"other\",\"accessToken\":\"other-key\"}";
  assert(login.pollLogin() == Error::Ok && fixture::accountClears == 2);
}
'''
        run(program, sources=[SAX / "src/StreamingJsonParser.cpp"])

    def test_transport_rejects_auth_before_body_and_cancels_waits(self):
        program = r'''
#include <cassert>
#include <string>
#include "WeReadHttpClient.h"
int main() {
  using namespace WeReadHttpClient;
  uint8_t buffer[256];
  RequestOptions options;
  options.readBuffer = buffer; options.readBufferSize = sizeof(buffer); options.timeoutMs = 50;
  options.shouldAbort = fixture::cancel;
  options.acceptStatus = [](int status) { return status != 401 && status != 403; };
  int status = -1, dataCalls = 0;
  const auto sink = [&](const uint8_t*, size_t) { ++dataCalls; return true; };
  for (int code : {401, 403}) {
    for (const char* body : {"", "<html>login required</html>", "{\"errcode\":-2012}"}) {
      fixture::response = "HTTP/1.1 " + std::to_string(code) + " Forbidden\r\nContent-Length: 99999\r\n\r\n" + body;
      assert(request("https://weread.qq.com/web/shelf/sync", options, sink, {}, status) == Result::Aborted);
      assert(status == code && dataCalls == 0 && !fixture::callbackHeld);
    }
  }
  for (const char* response : {"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}",
                              "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n"}) {
    fixture::response = response; dataCalls = 0;
    assert(request("https://weread.qq.com/web/shelf/sync", options, sink, {}, status) == Result::Ok);
    assert(dataCalls == 1 && !fixture::callbackHeld);
  }
  for (bool oneShot : {false, true}) {
    fixture::oneShot = oneShot;
    for (auto phase : {fixture::Phase::Handshake, fixture::Phase::Write, fixture::Phase::Read, fixture::Phase::Body}) {
      fixture::phase = fixture::Phase::Idle; fixture::cancelAt = phase;
      fixture::holdHandshake = true; fixture::cancellations = 0;
      fixture::response = phase == fixture::Phase::Read ? "" : "HTTP/1.1 200 OK\r\nContent-Length: 99999\r\n\r\n{}";
      assert(request("https://weread.qq.com/web/shelf/sync", options, sink, {}, status) == Result::Cancelled);
      assert(fixture::cancellations > 0 && !fixture::callbackHeld);
      if (oneShot) assert(fixture::cancellations == 1);
    }
  }
  fixture::phase = fixture::Phase::Idle; fixture::cancelAt = fixture::Phase::Idle;
  fixture::holdHandshake = false; fixture::response.clear();
  assert(request("https://weread.qq.com/web/shelf/sync", options, sink, {}, status) == Result::NetworkError);
  fixture::response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
  assert(request("https://weread.qq.com/web/shelf/sync", options,
                 [](const uint8_t*, size_t) { return false; }, {}, status) == Result::Aborted);
}
'''
        run(program, headers={
            "Arduino.h": ARDUINO, "SecureClient.h": SECURE,
            "Logging.h": "#pragma once\n#define LOG_DBG(...) ((void)0)\n#define LOG_INF(...) ((void)0)\n#define LOG_ERR(...) ((void)0)\n",
            "WiFi.h": "#pragma once\nusing wifi_mode_t=int; constexpr int WIFI_MODE_STA=1, WL_CONNECTED=3;\ninline struct { int getMode() { return 1; } int status() { return 3; } } WiFi;\n",
        }, sources=[API / "WeReadHttpClient.cpp"], defines=["FREEINK_NET_WOLFSSL=1", "CROSSPOINT_VERSION=\"test\""])

    def test_authentication_recovery_and_one_retry_limit(self):
        source = (API / "WeReadClient.cpp").read_text()
        header = (API / "WeReadClient.h").read_text()
        enums = method(header, "enum class Error") + ";\n"
        enums += "class Operation { public:\n"
        for name in ("Kind", "Event", "Phase"):
            enums += method(header, "enum class " + name) + ";\n"
        program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define LOG_INF(...) ((void)0)
unsigned long millis() { return 100; }
constexpr unsigned long kNetworkRetryBaseMs = 1000;
namespace WeReadHttpClient { using AbortCallback = bool (*)(void*); bool networkReady() { return true; } }
namespace WeReadStore {
bool clearOk = true; int clears = 0;
bool clearSession() { ++clears; return clearOk; }
}
''' + enums + r'''
struct Session { char vid[64] = "account", rt[384] = "old-token"; void clear() { vid[0]=rt[0]=0; } } session_;
struct { int resets=0; void reset() { ++resets; } } bookSession_;
Phase phase_=Phase::SyncShelf, resumePhase_=Phase::Idle;
Kind kind_=Kind::Sync; Error error_=Error::Ok;
bool cancelRequested_=false, renewalAttempted_=false, loginRecoveryAttempted_=false, loginConfirmed_=false;
bool initialProgressValid_=false; float initialProgressFraction_=0.0f;
Error nextRequestError_=Error::Ok;
WeReadHttpClient::AbortCallback shouldCancel_=nullptr; void* cancelContext_=nullptr;
uint8_t requestAttempt_=0; static constexpr uint8_t kMaxRequestAttempts=3;
unsigned long nextActionAt_=0, loginStartedAt_=0;
char previousVid_[64]{}, loginUid_[128]{}, cookie_[896]{}, url_[512]{}, psvts_[128]{};
void startLogin(Phase); void requestAuthentication(Phase); void requestSucceeded();
bool authenticating() const; static bool checkCancellation(void*);
Event handleRequestError(Error, Phase); Event reauthenticateChapter();
Event stepFetchProgress();
Error fetchProgressOnce(bool) { return nextRequestError_; }
float normalizedRemoteProgress() { return 0.25f; }
Event cancelNow() { phase_=Phase::Cancelled; error_=Error::Cancelled; return Event::Cancelled; }
Event fail(Error e) { if(e==Error::Cancelled) return cancelNow(); phase_=Phase::Failed; error_=e; return Event::Failed; }
};
'''
        for name in ("void Operation::startLogin", "void Operation::requestAuthentication",
                     "void Operation::requestSucceeded", "bool Operation::authenticating",
                     "bool Operation::checkCancellation", "Operation::Event Operation::handleRequestError",
                     "Operation::Event Operation::reauthenticateChapter"):
            program += method(source, name) + "\n"
        program += "Operation::Event Operation::stepFetchProgress() { switch(phase_) {\n"
        program += method(method(source, "Operation::Event Operation::step("), "case Phase::FetchProgress:")
        program += "\ndefault: assert(false); return Event::None;\n} }\n"
        program += r'''
int main() {
  using Phase = Operation::Phase; using Event = Operation::Event; using Kind = Operation::Kind;
  Operation op;
  assert(op.handleRequestError(Error::SessionExpired, Phase::SyncShelf) == Event::None);
  assert(op.authenticating() && op.resumePhase_ == Phase::SyncShelf);
  assert(op.phase_ == Phase::LoginUid && !op.renewalAttempted_);
  assert(!op.cookie_[0] && !op.session_.vid[0] && std::string(op.previousVid_) == "account");
  op.startLogin(Phase::SyncShelf); // Refreshing a QR challenge must retain the old account identity.
  assert(std::string(op.previousVid_) == "account");
  op.phase_ = Phase::SyncShelf; // A successful scan resumes the interrupted operation.
  op.requestSucceeded();
  assert(op.handleRequestError(Error::SessionExpired, Phase::SyncShelf) == Event::Failed);
  assert(op.error_ == Error::SessionExpired && !op.authenticating());
  Operation network;
  int clears = WeReadStore::clears;
  assert(network.handleRequestError(Error::Network, Phase::SyncShelf) == Event::None);
  assert(network.phase_ == Phase::SyncShelf && WeReadStore::clears == clears);
  Operation download; download.kind_ = Kind::Download;
  strcpy(download.psvts_, "stale");
  assert(download.handleRequestError(Error::SessionExpired, Phase::FetchText1) == Event::None);
  assert(download.resumePhase_ == Phase::LoadChapter && !download.psvts_[0]);
  Operation toc; toc.kind_=Kind::Download;
  assert(toc.handleRequestError(Error::SessionExpired, Phase::FetchToc)==Event::None);
  assert(toc.resumePhase_==Phase::FetchToc);
  Operation challenge; challenge.phase_=Phase::LoginUid; challenge.resumePhase_=Phase::SyncShelf;
  assert(challenge.handleRequestError(Error::SessionExpired, Phase::LoginUid)==Event::None);
  assert(challenge.resumePhase_==Phase::SyncShelf);
  Operation report; report.kind_=Kind::ProgressSync;
  assert(report.handleRequestError(Error::SessionExpired, Phase::SendProgressReport)==Event::None);
  assert(report.phase_==Phase::Renew && report.resumePhase_==Phase::FetchProgressReader);
  for(Error error:{Error::Ok,Error::Network,Error::Cancelled,Error::SessionExpired}) {
    Operation prefetch; prefetch.kind_=Kind::Download; prefetch.phase_=Phase::FetchProgress;
    prefetch.nextRequestError_=error;
    const auto event=prefetch.stepFetchProgress();
    if(error==Error::Cancelled) {
      assert(event==Event::Cancelled && prefetch.phase_==Phase::Cancelled);
    } else if(error==Error::SessionExpired) {
      assert(prefetch.authenticating() && prefetch.resumePhase_==Phase::FetchProgress);
    } else {
      assert(event==Event::None && prefetch.phase_==Phase::OpenToc);
      assert(prefetch.initialProgressValid_==(error==Error::Ok));
    }
  }
  Operation progress; progress.kind_ = Kind::ProgressSync;
  progress.requestAuthentication(Phase::FetchProgress);
  assert(progress.phase_ == Phase::Renew);
  progress.requestAuthentication(Phase::FetchProgress);
  assert(progress.phase_ == Phase::Failed && progress.error_ == Error::SessionExpired);
  Operation storage; WeReadStore::clearOk = false;
  storage.requestAuthentication(Phase::SyncShelf);
  assert(storage.phase_ == Phase::Failed && storage.error_ == Error::SdCard);
  Operation cancel; int polls=0;
  cancel.shouldCancel_ = [](void* context) { ++*static_cast<int*>(context); return true; };
  cancel.cancelContext_ = &polls;
  assert(Operation::checkCancellation(&cancel)); assert(Operation::checkCancellation(&cancel));
  assert(polls == 1);
  assert(cancel.handleRequestError(Error::Cancelled, Phase::SyncShelf) == Event::Cancelled);
}
'''
        run(program)

    def test_shelf_covers_use_sources_before_requesting_details(self):
        source = (API / "WeReadClient.cpp").read_text()
        header = (API / "WeReadClient.h").read_text()
        program = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
namespace WeReadProtocol { enum class ImageType { None,Jpeg }; }
namespace WeReadStore {
struct ShelfRecord { char bookId[64]="book", coverUrl[512]="https://cover"; };
std::string bookDirectory(const char* id) { return id; }
std::string coverPath(const std::string& dir) { return dir+"/cover.bmp"; }
}
struct { bool bmp=false; bool exists(const char*) { return bmp; } } Storage;
bool sourceExists=false;
WeReadProtocol::ImageType findCoverSource(const std::string&, std::string&) {
  return sourceExists ? WeReadProtocol::ImageType::Jpeg : WeReadProtocol::ImageType::None;
}
WeReadProtocol::ImageType selectCoverUrl(const char*, const char* shelf, char* out, size_t size) {
  snprintf(out,size,"%s",strcmp(shelf,"https://cover")==0 ? shelf : "");
  return out[0] ? WeReadProtocol::ImageType::Jpeg : WeReadProtocol::ImageType::None;
}
struct Operation {
''' + "\n".join(method(header, "enum class " + name) + ";" for name in ("Phase", "Kind", "Event")) + r'''
Phase phase_=Phase::Idle, resumePhase_=Phase::Idle;
bool sessionValid=true, beginOk=true;
WeReadStore::ShelfRecord book_;
std::unique_ptr<char[]> shelfCoverUrl_;
char url_[512]={}; std::string bookDir_, outputPath_;
WeReadProtocol::ImageType coverType_=WeReadProtocol::ImageType::None;
bool begin(Kind kind, const WeReadStore::ShelfRecord* book) {
  assert(kind==Kind::Detail);
  if(!beginOk) return false;
  book_=*book; shelfCoverUrl_=std::make_unique<char[]>(512);
  strcpy(shelfCoverUrl_.get(),book->coverUrl);
  phase_=sessionValid ? Phase::PrepareDetail : Phase::LoginUid; return true;
}
bool authenticating() const { return phase_==Phase::LoginUid; }
bool beginCover(const WeReadStore::ShelfRecord& book);
Event step() { switch(phase_) {
''' + method(method(source, "Operation::Event Operation::step("), "case Phase::PrepareCover:") + r'''
default: assert(false); return Event::None;
} }
};
''' + method(source, "bool Operation::beginCover(") + r'''
int main() {
  using Phase=Operation::Phase; using Event=Operation::Event;
  WeReadStore::ShelfRecord book;
  Operation cached; Storage.bmp=true;
  assert(cached.beginCover(book)); assert(cached.step()==Event::Complete);
  assert(cached.phase_==Phase::Complete);
  Storage.bmp=false; sourceExists=true;
  Operation source; assert(source.beginCover(book)); source.step();
  assert(source.phase_==Phase::ConvertCover);
  sourceExists=false;
  Operation remote; assert(remote.beginCover(book)); remote.step();
  assert(remote.phase_==Phase::FetchCover && strcmp(remote.url_,book.coverUrl)==0);
  for(const char* url:{"","invalid"}) {
    strcpy(book.coverUrl,url);
    Operation fallback; assert(fallback.beginCover(book)); fallback.step();
    assert(fallback.phase_==Phase::PrepareDetail);
  }
  Operation login; login.sessionValid=false;
  assert(login.beginCover(book));
  assert(login.phase_==Phase::LoginUid && login.resumePhase_==Phase::PrepareCover);
  Operation failed; failed.beginOk=false; assert(!failed.beginCover(book));
}
'''
        run(program)

    def test_background_cover_failure_does_not_stop_the_page(self):
        source = ACTIVITY.read_text()
        header = (API / "WeReadClient.h").read_text()
        program = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <string>
#define LOG_ERR(...) ((void)0)
constexpr int WL_CONNECTED=3;
struct { int status() { return WL_CONNECTED; } } WiFi;
struct { bool exists(const char*) { return false; } } Storage;
namespace WeReadStore {
struct ShelfRecord { char bookId[64]="book"; };
std::string bookDirectory(const char* id) { return id; }
std::string coverPath(const std::string& dir) { return dir+"/cover.bmp"; }
}
namespace WeReadClient { struct Operation {
''' + method(header, "enum class Event") + r''';
bool active_=true; int resets=0, begins=0;
bool active() { return active_; }
void reset() { active_=false; ++resets; }
bool beginCover(const WeReadStore::ShelfRecord&) { active_=true; ++begins; return true; }
int error() { return 2; }
}; }
struct WeReadActivity {
bool shelfCoverStopped_=false;
uint32_t shelfCount_=3;
int shelfCoverPageStart_=0, shelfCoverCursor_=0, lastRead=-1;
std::atomic<int> shelfSelected_{0}; std::atomic<bool> shelfFrameInvalidated_{false};
WeReadClient::Operation operation_;
WeReadClient::Operation::Event next=WeReadClient::Operation::Event::Failed;
bool prepareNetworkMemory() { return true; }
int shelfItemsPerPage() { return 3; }
bool readShelf(int index, WeReadStore::ShelfRecord&) { lastRead=index; return true; }
auto stepOperation() { return next; }
void requestUpdate() {}
void advanceShelfCovers();
};
''' + method(source, "void WeReadActivity::advanceShelfCovers(") + r'''
int main() {
  using Event=WeReadClient::Operation::Event;
  WeReadActivity page;
  page.advanceShelfCovers();
  assert(!page.shelfCoverStopped_ && page.shelfCoverCursor_==1);
  assert(page.lastRead==1 && page.operation_.begins==1);
  page.next=Event::Complete; page.advanceShelfCovers();
  assert(page.lastRead==2 && page.shelfFrameInvalidated_);
  page.next=Event::Cancelled; page.advanceShelfCovers();
  assert(page.shelfCoverStopped_);
}
'''
        run(program)

    def test_statistics_lifetime_and_blocking_input_owner(self):
        source = ACTIVITY.read_text()
        program = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
unsigned long ticks=100; unsigned long millis() { return ticks; }
int restarts=0; void silentRestart();
void delay(int) {} void esp_wifi_deinit() {}
struct { int mode=1; int getMode() { return mode; } void disconnect(bool) {} void modeOff(int m) { mode=m; } } wifi;
struct WiFiType { int getMode() { return wifi.mode; } void disconnect(bool) {} void mode(int value) { wifi.modeOff(value); } } WiFi;
constexpr int WIFI_MODE_NULL=0, WIFI_OFF=0;
void silentRestart() { assert(wifi.mode==0); ++restarts; }
struct { bool saveOk=true; int releases=0;
 bool releaseMemoryForNetwork() { ++releases; return saveOk; }
 bool reloadAfterNetwork() { assert(false && "fragmented-heap restore may abort"); return false; }
} READING_STATS;
namespace WeReadClient { enum class Error { Ok,SdCard }; }
struct Activity { void onExit() {} };
struct RenderLock { template<class T> explicit RenderLock(T&) {} };
struct MappedInputManager {
 enum class Button { Back };
 bool released=false, home=false; int updates=0;
 void update(bool deferHome) { assert(deferHome); ++updates; }
 bool wasReleased(Button) { return released; } bool wasHomeGesture() { return home; }
};
struct WeReadActivity : Activity {
 enum class State { Home,Error }; std::atomic<State> state_{State::Home};
 WeReadClient::Error error_=WeReadClient::Error::Ok;
 bool readingStatsReleased_=false, wifiSessionActive_=true; uint32_t lastCancelPollAt_=0;
 std::atomic<bool> downloadRenderPending_{false}, stageRenderPending_{false};
 struct { void reset() {} } operation_;
 struct { bool isOpen() { return false; } void close() {} } shelfFile_;
 struct TabInfo {}; std::vector<TabInfo> mainTabs_;
 MappedInputManager mappedInput;
 void requestJobUpdate() {} bool prepareNetworkMemory(); bool pollNetworkCancel(); void onExit();
};
'''
        for name in ("bool WeReadActivity::prepareNetworkMemory", "bool WeReadActivity::pollNetworkCancel",
                     "void WeReadActivity::onExit"):
            program += method(source, name) + "\n"
        program += r'''
int main() {
  WeReadActivity offline; wifi.mode=0; offline.onExit(); assert(restarts==0);
  WeReadActivity failed; READING_STATS.saveOk=false;
  assert(!failed.prepareNetworkMemory());
  assert(failed.state_ == WeReadActivity::State::Error && !failed.readingStatsReleased_);
  wifi.mode=0; failed.onExit(); assert(restarts == 0);
  READING_STATS.saveOk=true; READING_STATS.releases=0;
  WeReadActivity success;
  assert(success.prepareNetworkMemory() && success.prepareNetworkMemory());
  assert(READING_STATS.releases == 1);
  wifi.mode=1; success.onExit(); assert(restarts == 1);
  WeReadActivity beforeWifi; assert(beforeWifi.prepareNetworkMemory());
  wifi.mode=0; beforeWifi.onExit(); assert(restarts == 2);
  success.mappedInput.released=true;
  assert(success.pollNetworkCancel()); assert(success.mappedInput.updates == 1);
  assert(!success.pollNetworkCancel()); assert(success.mappedInput.updates == 1);
  ticks += 10; success.mappedInput.released=false;
  assert(!success.pollNetworkCancel()); assert(success.mappedInput.updates == 2);
}
'''
        run(program, defines=["CONFIG_IDF_TARGET_ESP32C3=1"])


if __name__ == "__main__":
    unittest.main()
