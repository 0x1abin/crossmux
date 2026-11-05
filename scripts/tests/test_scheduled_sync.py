"""Offline host tests. CXX may name a compiler command, e.g. 'zig c++'."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest
from test_reading_ui_regressions import method

ROOT = Path(__file__).resolve().parents[2]


def run(program):
    with tempfile.TemporaryDirectory(prefix="crossmux-sync-") as directory:
        cpp = Path(directory) / "check.cpp"
        exe = Path(directory) / "check.exe"
        cpp.write_text(program, encoding="utf-8")
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run([*compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT), str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class ScheduledSyncTest(unittest.TestCase):
    def test_actual_pending_queue_retries_failures_and_skips_unchanged_success(self):
        source = (ROOT / 'src/activities/apps/weread/WeReadAutoSync.cpp').read_text(encoding='utf-8')
        run(r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
using String=std::string;
struct GfxRenderer {};
struct RenderLock {~RenderLock(){}};
uint32_t ticks=0;
uint32_t millis(){return ticks;}
void delay(unsigned v){ticks+=v;}
constexpr int WIFI_MODE_NULL=0,WIFI_OFF=0,WIFI_STA=1,WL_CONNECTED=3;
bool loginValid=true,allocationFails=false;
int eventValue=0,outcomeValue=0,steps=0;
struct {void setPowerSaving(bool){}} powerManager;
struct {int mode_=1,status_=3;int getMode(){return mode_;}int status(){return status_;}
 void persistent(bool){} void begin(const char*,const char*){}
 void disconnect(bool){} void mode(int v){mode_=v;}} WiFi;
void esp_wifi_deinit(){}
namespace NetworkStartup {bool setMode(GfxRenderer&,int){return true;}}
namespace TimeUtils {bool isClockValid(){return true;}}
struct {bool syncNow(){return true;}} halClock;
struct Credential{std::string ssid="test",password;};
struct {bool available=true;int getCredentialCount(){return 1;}void loadFromFile(){}
 std::string getLastConnectedSsid(){return "test";}
 std::optional<Credential> findCredential(const std::string&){return available?std::optional<Credential>(Credential{}):std::nullopt;}} WIFI_STORE;
struct {
 std::map<std::string,std::string> files;
 bool failWrite=false;
 bool ensureDirectoryExists(const char*){return true;}
 bool exists(const char* p){return files.count(p);}
 String readFile(const char* p){auto f=files.find(p);return f==files.end()?"":f->second;}
 bool writeFile(const char* p,const String& value){if(failWrite)return false;files[p]=value;return true;}
 bool remove(const char* p){return files.erase(p)!=0;}
} Storage;
namespace WeReadStore {
 std::string bookDirectory(const char* id){return std::string("/books/")+id;}
 struct Session{bool valid(){return loginValid;}void clear(){}};
 bool loadSession(Session&){return true;}
}
namespace WeReadClient {
 enum class Basis{None};
 struct ProgressSyncInput{float localFraction=.2f;unsigned localTocIndex=0,localOffset=4;Basis localOffsetBasis=Basis::None;};
 enum class ProgressSyncMode{AutoUpload};
 enum class ProgressSyncOutcome{LocalUploaded,AlreadySynced,SelectionRequired};
 struct Operation {
  enum class Event{Complete,Failed,Cancelled};
  bool beginProgressSync(const char*,ProgressSyncInput,ProgressSyncMode){return true;}
  Event step(void*,void*,bool(*)(void*),void*){++steps;return static_cast<Event>(eventValue);}
  struct Result{ProgressSyncOutcome outcome;};
  Result progressSyncResult(){return {static_cast<ProgressSyncOutcome>(outcomeValue)};}
 };
}
template<class T>std::unique_ptr<T> makeUniqueNoThrow(){return allocationFails?nullptr:std::make_unique<T>();}
namespace weread_auto {
char queuedBook[64]{};
WeReadClient::ProgressSyncInput queuedInput;
uint32_t startedAt=0;
bool expired(void*){return millis()-startedAt>=45000u;}
''' + method(source, 'void queue(') + method(source, 'void drain(') + r'''
}
int main(){
 GfxRenderer renderer;
 WeReadClient::ProgressSyncInput input;
 const char* pending="/books/book/auto_sync_pending";
 const char* local="/books/book/auto_sync_local";
 weread_auto::queue("book",input);assert(Storage.exists(pending));
 eventValue=1;weread_auto::drain(renderer);assert(Storage.exists(pending)&&!Storage.exists(local));
 weread_auto::queue("book",input);eventValue=0;outcomeValue=2;
 weread_auto::drain(renderer);assert(Storage.exists(pending)&&!Storage.exists(local));
 weread_auto::queue("book",input);outcomeValue=0;Storage.failWrite=true;
 weread_auto::drain(renderer);assert(Storage.exists(pending));
 Storage.failWrite=false;weread_auto::queue("book",input);
 weread_auto::drain(renderer);assert(!Storage.exists(pending)&&Storage.exists(local));
 const int before=steps;weread_auto::queue("book",input);weread_auto::drain(renderer);
 assert(steps==before&&!Storage.exists(pending));
 input.localOffset=9;weread_auto::queue("book",input);allocationFails=true;
 weread_auto::drain(renderer);assert(Storage.exists(pending)&&steps==before);
 allocationFails=false;weread_auto::queue("book",input);loginValid=false;
 weread_auto::drain(renderer);assert(Storage.exists(pending)&&steps==before);
 loginValid=true;weread_auto::queue("book",input);WiFi.mode_=0;WiFi.status_=0;WIFI_STORE.available=false;
 weread_auto::drain(renderer);assert(Storage.exists(pending)&&steps==before);
}
''')

    def test_actual_scheduler_waits_for_wifi_and_retains_retry_deadline(self):
        source = (ROOT / 'src/activities/apps/airpage/AirPageActivity.cpp').read_text(encoding='utf-8')
        run(r'''
#include <cassert>
#include "src/activities/apps/airpage/AirPageSchedule.h"
#define CROSSPOINT_EMULATED 1
uint32_t now=0;
uint32_t millis(){return now;}
namespace airpage {
struct AirPageConnection {
 enum class State {Off,WifiConnecting,WifiOnline};
 bool live=false,connected=false,canJoin=true;
 State status=State::Off;
 int joins=0,stops=0;
 bool realtime(){return live;}
 bool wifiConnected(){return connected;}
 State state(){return status;}
 bool connectScheduled(){++joins;if(canJoin)status=State::WifiConnecting;return canJoin;}
 void stop(){++stops;connected=false;status=State::Off;}
};
}
struct AirPageActivity {
 enum class Phase{Idle,Fetching};
 enum class Screen{Qr,Settings,Image};
 uint8_t syncInterval_=1;
 uint32_t lastScheduledMs_=0;
 bool imageNeedsDisplay_=false,scheduledFetch_=false;
 Phase phase_=Phase::Idle;
 Screen screen_=Screen::Image;
 struct {bool active=false;bool isActive(){return active;}} imageMenu_;
 airpage::AirPageConnection connection_;
 int fetches=0;
 void queueFetch(){++fetches;phase_=Phase::Fetching;}
 void pumpSchedule();
};
''' + method(source, 'void AirPageActivity::pumpSchedule()') + r'''
int main(){
 AirPageActivity a;
 now=299999;a.pumpSchedule();assert(a.connection_.joins==0);
 now=300000;a.pumpSchedule();assert(a.connection_.joins==1&&a.scheduledFetch_);
 a.pumpSchedule();assert(a.fetches==0);
 a.connection_.connected=true;a.pumpSchedule();assert(a.fetches==1);
 AirPageActivity failed;now=300000;failed.pumpSchedule();
 failed.connection_.status=airpage::AirPageConnection::State::Off;
 failed.pumpSchedule();assert(!failed.scheduledFetch_&&failed.connection_.stops==1);
 now=599999;failed.pumpSchedule();assert(failed.connection_.joins==1);
 now=600000;failed.pumpSchedule();assert(failed.connection_.joins==2);
 AirPageActivity settings;settings.screen_=AirPageActivity::Screen::Settings;
 settings.pumpSchedule();assert(settings.connection_.joins==0);
 AirPageActivity realtime;realtime.connection_.live=true;
 realtime.pumpSchedule();assert(realtime.connection_.joins==0);
 AirPageActivity off;off.syncInterval_=0;off.pumpSchedule();assert(off.connection_.joins==0);
}
''')

    def test_schedule_off_boundary_and_wrap(self):
        run(r'''
#include <cassert>
#include "src/activities/apps/airpage/AirPageSchedule.h"
int main() {
 using airpage::syncDue;
 assert(!syncDue(1000000,0,0));
 assert(!syncDue(299999,0,5));
 assert(syncDue(300000,0,5));
 assert(syncDue(301000,0,5));
 const uint32_t last=UINT32_MAX-1000;
 assert(!syncDue(last+299999u,last,5));
 assert(syncDue(last+300000u,last,5));
 assert(!syncDue(21599999,0,360));
 assert(syncDue(21600000,0,360));
}
''')

    def test_baseline_detects_cloud_changes(self):
        run(r'''
#include <cassert>
#include <cstring>
#include <limits>
#include "lib/WeReadWebApi/src/WeReadProgressPolicy.h"
struct Remote {
 char chapterUid[64]="42";
 float percent=10;
 uint32_t appIdHash=123,chapterOffset=456,updateTime=100;
 bool hasChapterOffset=true,hasUpdateTime=true,hasAppId=true;
};
int main() {
 Remote r;
 char baseline[192],changed[192];
 assert(WeReadClient::formatProgressBaseline(r,baseline,sizeof baseline));
 const auto differs=[&](const Remote& value) {
  assert(WeReadClient::formatProgressBaseline(value,changed,sizeof changed));
  assert(strcmp(baseline,changed)!=0);
 };
 auto v=r; ++v.updateTime; differs(v);
 v=r; ++v.appIdHash; differs(v);
 v=r; ++v.chapterOffset; differs(v);
 v=r; v.percent=10.00001f; differs(v);
 v=r; strcpy(v.chapterUid,"43"); differs(v);
 v=r; v.hasAppId=false; differs(v);
 v=r; v.hasChapterOffset=false; differs(v);
 v=r; v.hasUpdateTime=false;
 assert(!WeReadClient::formatProgressBaseline(v,changed,sizeof changed));
 v=r; v.percent=std::numeric_limits<float>::quiet_NaN();
 assert(!WeReadClient::formatProgressBaseline(v,changed,sizeof changed));
 assert(!WeReadClient::formatProgressBaseline(r,changed,8));
}
''')

    def test_actual_decision_preserves_conflicts(self):
        source = (ROOT / 'lib/WeReadWebApi/src/WeReadClient.cpp').read_text(encoding='utf-8')
        run(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#define LOG_INF(...) logInfo(__VA_ARGS__)
template<class... A> void logInfo(A&&...) {}
enum class Error {Ok,Unavailable,Clock};
enum class LocalOffsetBasis {None,VisibleText,RawXhtmlUtf16};
enum class ProgressSyncMode {Compare,ApplyRemote,UploadLocal,AutoUpload};
enum class ProgressSyncOutcome {Pending,AlreadySynced,SelectionRequired,ApplyRemote,LocalUploaded};
namespace TimeUtils {uint32_t getCurrentValidTimestamp(){return 123;}}
namespace WeReadStore {
 template<class... A> bool mapVisibleOffsetToChapter(A&&...){return true;}
 template<class... A> bool mapNativeOffsetToChapter(A&&...){return true;}
 template<class... A> bool mapFractionToChapter(A&&...){return true;}
}
bool baselineMatches=false;
int baselineWrites=0;
bool rememberRemoteProgress(const char*,int){++baselineWrites;return true;}
bool matchesRemoteBaseline(const char*,int){return baselineMatches;}
bool makeReaderReferer(const char*,const char*,std::string&){return true;}
struct Operation {
 enum class ProgressAction {AlreadySynced,SelectDirection,ApplyRemote,UploadLocal};
 float normalizedRemoteProgress(){return .1f;}
 bool sameRemotePosition(){return same;}
 static ProgressAction progressAction(ProgressSyncMode mode,bool same){
  if(same)return ProgressAction::AlreadySynced;
  if(mode==ProgressSyncMode::Compare)return ProgressAction::SelectDirection;
  if(mode==ProgressSyncMode::ApplyRemote)return ProgressAction::ApplyRemote;
  return ProgressAction::UploadLocal;
 }
 Error decideProgress();
 bool same=false;
 struct Remote {float percent=0; operator int()const{return 0;}};
 struct {Remote remote;ProgressSyncOutcome outcome=ProgressSyncOutcome::Pending;} progressSyncResult_;
 struct {LocalOffsetBasis localOffsetBasis=LocalOffsetBasis::None;unsigned localTocIndex=0,localOffset=0;float localFraction=.2f;} progressSyncInput_;
 ProgressSyncMode progressSyncMode_=ProgressSyncMode::AutoUpload;
 struct {char bookId[16]="book";} book_;
 struct {char chapterUid[16]="chapter";} chapter_;
 std::string tocPath_,referer_;
 unsigned progressChapterOffset_=0,progressUploadStartedAt_=0;
};
''' + method(source, 'Error Operation::decideProgress()') + r'''
int main(){
 Operation conflict;
 assert(conflict.decideProgress()==Error::Ok);
 assert(conflict.progressSyncResult_.outcome==ProgressSyncOutcome::SelectionRequired);
 assert(conflict.progressUploadStartedAt_==0);
 baselineMatches=true;
 Operation upload;
 assert(upload.decideProgress()==Error::Ok);
 assert(upload.progressSyncResult_.outcome==ProgressSyncOutcome::Pending);
 assert(upload.progressUploadStartedAt_==123);
 baselineMatches=false;
 Operation same; same.same=true;
 assert(same.decideProgress()==Error::Ok);
 assert(same.progressSyncResult_.outcome==ProgressSyncOutcome::AlreadySynced);
 assert(baselineWrites==1);
 Operation manual;manual.progressSyncMode_=ProgressSyncMode::UploadLocal;
 assert(manual.decideProgress()==Error::Ok);
 assert(manual.progressUploadStartedAt_==123);
}
'''.replace('  const char* basis = "fraction";', '  [[maybe_unused]] const char* basis = "fraction";'))


if __name__ == '__main__':
    unittest.main()
