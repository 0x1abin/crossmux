"""Run production WiFi entry points against a failing driver; retries must be explicit."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class WifiStartupFailureTest(unittest.TestCase):
    def test_failed_init_stops_scan_and_connection_until_retry(self):
        source = (ROOT / 'src/activities/network/WifiSelectionActivity.cpp').read_text()
        methods = '\n'.join(method(source, signature) for signature in (
            'void WifiSelectionActivity::showNetworkError()',
            'void WifiSelectionActivity::startWifiScan(',
            'void WifiSelectionActivity::attemptConnection()',
            'void WifiSelectionActivity::onScanEvent('))
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
constexpr int WIFI_STA = 1, WIFI_SCAN_FAILED = -2;
constexpr int WIFI_ALL_CHANNEL_SCAN = 0, WIFI_CONNECT_AP_BY_SIGNAL = 0;
unsigned long millis() { return 0; }
void delay(int) {}
bool readStationMac(uint8_t (&)[6]) { return false; }
namespace fui { struct ActionEvent {}; }
enum class WifiSelectionState { SCANNING, NETWORK_ERROR, NETWORK_LIST, AUTO_CONNECTING, CONNECTING };
namespace NetworkStartup {
int starts = 0;
bool success = false;
bool setMode(int, int) { ++starts; return success; }
}
struct {
 int disconnects = 0, scans = 0, connections = 0, scanResult = -1;
 void persistent(bool) {}
 void disconnect(bool = false, bool = false) { ++disconnects; }
 int scanNetworks(bool) { ++scans; return scanResult; }
 void setScanMethod(int) {}
 void setSortMethod(int) {}
 void setHostname(const char*) {}
 void begin(const char*, const char* = nullptr) { ++connections; }
} WiFi;
class WifiSelectionActivity {
 public:
 int renderer = 0, realNetworkCount = 0;
 bool autoConnecting = true, manualNetworkListRequested = false, selectedRequiresPassword = false;
 unsigned long connectionStartTime = 0;
 std::string connectedIP, connectionError, selectedSSID, enteredPassword;
 std::vector<int> networks, networkStatuses, networkRowItems;
 struct { void reset() {} } listNav;
 struct { void clearTapFlash() {} } app;
 WifiSelectionState state = WifiSelectionState::NETWORK_LIST;
 void requestUpdate() {}
 void showNetworkError();
 void startWifiScan(bool autoScan = false);
 void attemptConnection();
 static void onScanEvent(const fui::ActionEvent&, void*);
};
''' + methods + r'''
int main() {
 WifiSelectionActivity activity;
 activity.startWifiScan(true);
 assert(activity.state == WifiSelectionState::NETWORK_ERROR && !activity.autoConnecting);
 assert(NetworkStartup::starts == 1 && WiFi.disconnects == 0 && WiFi.scans == 0);
 activity.attemptConnection();
 assert(activity.state == WifiSelectionState::NETWORK_ERROR);
 assert(NetworkStartup::starts == 2 && WiFi.disconnects == 0 && WiFi.connections == 0);
 // Retry is routed from the error screen and succeeds after memory becomes available.
 NetworkStartup::success = true;
 WifiSelectionActivity::onScanEvent({}, &activity);
 assert(activity.state == WifiSelectionState::SCANNING);
 assert(NetworkStartup::starts == 3 && WiFi.disconnects == 1 && WiFi.scans == 1);
 // A stale/double touch during scanning must not start another scan.
 WifiSelectionActivity::onScanEvent({}, &activity);
 assert(NetworkStartup::starts == 3 && WiFi.scans == 1);
 WiFi.scanResult = WIFI_SCAN_FAILED;
 activity.startWifiScan();
 assert(activity.state == WifiSelectionState::NETWORK_ERROR);
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
