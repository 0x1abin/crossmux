"""Exercise production startup/cleanup against allocation and socket failures."""
from pathlib import Path
import re
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class WebServerStartupFailureTest(unittest.TestCase):
    def test_failed_start_cleans_up_and_can_retry(self):
        source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
        begin = method(source, 'bool CrossPointWebServer::begin()')
        stop = method(source, 'void CrossPointWebServer::stop()')
        handlers = sorted(set(re.findall(r'\b(handle\w+)\(', begin)))
        declarations = '\n'.join('template<class... T> void '+name+'(T&&...) {}' for name in handlers)
        run_cpp(r'''
#include <cassert>
#include <memory>
#include <new>
#include <string>
#include <utility>
template<class... T> void logMessage(T&&...) {}
#define LOG_DBG(...) logMessage(__VA_ARGS__)
#define LOG_ERR(...) logMessage(__VA_ARGS__)
using String = std::string;
using wifi_mode_t = int;
constexpr int WIFI_MODE_STA=1, WIFI_MODE_AP=2, WL_CONNECTED=3, HTTP_GET=0, HTTP_POST=1;
constexpr int LOCAL_UDP_PORT=8134, FILE_LIST_BATCH_CAPACITY=1400;
int allocation=0, failAt=0, httpLive=0, wsLive=0, httpListening=0, wsListening=0;
bool udpSuccess=true;
void delay(int) {}
struct Address { String toString() const { return "192.168.4.1"; } };
struct {
 int mode=WIFI_MODE_AP;
 int getMode() { return mode; }
 int status() { return 0; }
 int softAPgetStationNum() { return 0; }
 void setSleep(bool) {}
 void setAutoReconnect(bool) {}
 Address softAPIP() { return {}; }
 Address localIP() { return {}; }
} WiFi;
struct { int getFreeHeap() { return 50000; } } ESP;
struct WebDAVHandler {
 static void* operator new(size_t size, const std::nothrow_t&) noexcept {
  return ++allocation == failAt ? nullptr : ::operator new(size, std::nothrow);
 }
 static void operator delete(void* p) noexcept { ::operator delete(p); }
};
struct WebServer {
 std::unique_ptr<WebDAVHandler> handler;
 explicit WebServer(int) { ++httpLive; }
 ~WebServer() { --httpLive; }
 template<class... T> void on(T&&...) {}
 template<class T> void onNotFound(T) {}
 void enableCORS(bool) {}
 void collectHeaders(const char**, int) {}
 void addHandler(WebDAVHandler* p) { handler.reset(p); }
 void begin() { ++httpListening; }
 void stop() { httpListening=0; }
};
using CrossPointHttpServer = WebServer;
struct WebSocketsServer {
 explicit WebSocketsServer(int) { ++wsLive; }
 ~WebSocketsServer() { --wsLive; }
 void begin() { ++wsListening; }
 void onEvent(void(*)()) {}
 void close() { wsListening=0; }
};
template<class T, class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... args) {
 if (++allocation == failAt) return nullptr;
 return std::make_unique<T>(std::forward<A>(args)...);
}
using Buffer=std::unique_ptr<unsigned char[]>;
Buffer makeWebBuffer(size_t size) {
 if (++allocation == failAt) return nullptr;
 return std::make_unique<unsigned char[]>(size);
}
bool wsUploadInProgress=false, wsUploadFile=false;
class CrossPointWebServer;
CrossPointWebServer* wsInstance=nullptr;
struct UploadState { static constexpr int UPLOAD_BUFFER_SIZE=4096; Buffer buffer; };
struct FontUploadState { static constexpr int BUFFER_SIZE=4096; Buffer buffer; };
class CrossPointWebServer {
 public:
 bool running=false, apMode=false, udpActive=false;
 int port=80, wsPort=81;
 std::unique_ptr<WebServer> server;
 std::unique_ptr<WebSocketsServer> wsServer;
 Buffer fileListBatch;
 UploadState upload;
 FontUploadState fontUpload;
 struct {
  bool listening=false, bufferAllocated=false;
  bool begin(int) { bufferAllocated=true; return listening=udpSuccess; }
  void stop() { listening=false; bufferAllocated=false; }
 } udp;
 static void wsEventCallback() {}
 void abortWsUpload(const char*) { assert(false); }
 bool begin();
 void stop();
''' + declarations + '\n};\n' + begin + '\n' + stop + r'''
void assertStopped(const CrossPointWebServer& web) {
 assert(!web.running && !web.server && !web.wsServer && !web.fileListBatch);
 assert(!web.upload.buffer && !web.fontUpload.buffer && !web.udpActive && !web.udp.listening && !web.udp.bufferAllocated);
 assert(!wsInstance && httpLive==0 && wsLive==0 && httpListening==0 && wsListening==0);
}
int main() {
 CrossPointWebServer web;
 WiFi.mode=0;
 assert(!web.begin());
 assertStopped(web);
 WiFi.mode=WIFI_MODE_AP;
 // WebServer, both buffers, WebDAV, WebSocket and file-list buffer allocations.
 for (int failure=1; failure<=6; ++failure) {
  allocation=0; failAt=failure;
  assert(!web.begin());
  assertStopped(web);
  allocation=0; failAt=0;
  assert(web.begin());
  assert(web.running && web.udpActive && wsInstance==&web);
  const int before=allocation;
  assert(web.begin() && allocation==before);
  web.stop(); web.stop();
  assertStopped(web);
 }
 allocation=0; udpSuccess=false;
 assert(!web.begin());
 assertStopped(web);
 udpSuccess=true;
 assert(web.begin());
 // Transfer suspension can destroy the listener before stop; clear its callback owner too.
 web.wsServer->close();
 web.wsServer.reset();
 web.stop();
 assertStopped(web);
}
''')

    def test_web_buffers_prefer_psram_and_fall_back(self):
        source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
        body = method(source, 'memory::ByteBuffer makeWebBuffer(')
        run_cpp(r'''
#include <cassert>
#include <cstddef>
#include <memory>
namespace memory {
using ByteBuffer = std::unique_ptr<unsigned char[]>;
bool psramAvailable = true, internalAvailable = true;
int internalCalls = 0;
ByteBuffer makePsramByteBufferNoThrow(size_t size) {
 return psramAvailable ? std::make_unique<unsigned char[]>(size) : nullptr;
}
ByteBuffer makeInternalByteBufferNoThrow(size_t size) {
 ++internalCalls;
 return internalAvailable ? std::make_unique<unsigned char[]>(size) : nullptr;
}
}
''' + body + r'''
int main() {
 assert(makeWebBuffer(4096) && memory::internalCalls == 0);
 memory::psramAvailable = false;
 assert(makeWebBuffer(4096) && memory::internalCalls == 1);
 memory::internalAvailable = false;
 assert(!makeWebBuffer(1400) && memory::internalCalls == 2);
}
''')

    def test_failed_ap_init_does_not_start_services(self):
        source = (ROOT / 'src/activities/network/CrossPointWebServerActivity.cpp').read_text()
        body = method(source, 'void CrossPointWebServerActivity::startAccessPoint()')
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
constexpr int WIFI_AP = 2, AP_CHANNEL = 1, AP_MAX_CONNECTIONS = 4, DNS_PORT = 53;
const char* AP_PASSWORD = "";
const char* AP_SSID = "reader";
const char* AP_HOSTNAME = "reader";
int apCalls = 0, mdnsCalls = 0, dnsCalls = 0, webCalls = 0;
namespace NetworkStartup {
bool success = false;
bool setMode(int, int) { return success; }
}
void delay(int) {}
struct IPAddress { int operator[](int) const { return 1; } };
struct {
 bool success = true;
 bool softAP(const char*, const char*, int, bool, int) { ++apCalls; return success; }
 IPAddress softAPIP() { return {}; }
} WiFi;
namespace DNSReplyCode { constexpr int NoError = 0; }
struct DNSServer {
 void setErrorReplyCode(int) {}
 void start(int, const char*, IPAddress) { ++dnsCalls; }
};
DNSServer* dnsServer = nullptr;
void stopDnsServer() { delete dnsServer; dnsServer = nullptr; }
void restartMdns(const char*, const char*) { ++mdnsCalls; }
class CrossPointWebServerActivity {
 public:
 int renderer = 0, homeCalls = 0;
 std::string connectedIP, connectedSSID;
 void onGoHome() { ++homeCalls; }
 void startWebServer() { ++webCalls; }
 void startAccessPoint();
};
''' + body + r'''
int main() {
 CrossPointWebServerActivity activity;
 activity.startAccessPoint();
 assert(activity.homeCalls == 1 && apCalls == 0);
 assert(mdnsCalls == 0 && dnsCalls == 0 && webCalls == 0);
 NetworkStartup::success = true;
 WiFi.success = false;
 activity.startAccessPoint();
 assert(activity.homeCalls == 2 && apCalls == 1);
 assert(mdnsCalls == 0 && dnsCalls == 0 && webCalls == 0);
 WiFi.success = true;
 activity.startAccessPoint();
 assert(activity.homeCalls == 2 && apCalls == 2);
 assert(mdnsCalls == 1 && dnsCalls == 1 && webCalls == 1);
 stopDnsServer();
}
''')


if __name__ == '__main__':
    unittest.main()
