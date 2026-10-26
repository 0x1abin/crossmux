"""Exercise production reader sync handoff and plugin-only WiFi completion."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
def method(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]
reader = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()
sync = (ROOT / "src/activities/reader/KOReaderSyncActivity.cpp").read_text()
harness = r"""
#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <Memory.h>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define STR_SYNC_PROGRESS 1
#define STR_CALC_HASH 2
const char* tr(int) { return "sync"; }
bool locked=false, allocationOk=true, saveOk=true;
int allocations=0, saves=0, releases=0, mappings=0, replacements=0, flushes=0, destroyed=0;
std::vector<int> phases;
struct { bool credentials=false; bool hasCredentials()const{return credentials;} } KOREADER_STORE;
struct { size_t free=128*1024,largest=96*1024;size_t getFreeHeap(){return free;}size_t getMaxAllocHeap(){return largest;} } ESP;
struct CrossPointPosition {int spineIndex=2;bool hasVisibleTextOffset=true;uint32_t visibleTextOffset=17;};
struct SavedProgressPosition {std::string xpath;float percentage=0;};
struct Cache {void releaseSdFontCaches(){assert(locked);++releases;phases.push_back(3);} } cache;
struct GfxRenderer { Cache* getFontCacheManager(){return &cache;}
 struct FrameBufferLoan {explicit FrameBufferLoan(GfxRenderer&){assert(locked&&releases==1);phases.push_back(4);}};
};
struct RenderLock {RenderLock(){assert(!locked);locked=true;}template<class T>explicit RenderLock(T&):RenderLock(){}~RenderLock(){locked=false;}};
struct Epub {std::string getPath()const{return "/book.epub";}};
struct Section {int currentPage=4;int estimatedTotalPages()const{return 10;}};
struct ImageBlock {static void releaseRenderCache(){assert(locked);phases.push_back(2);}static void setExtractor(void*,void*){}};
struct ProgressMapper {static SavedProgressPosition toSavedProgress(const std::shared_ptr<Epub>& epub,CrossPointPosition pos){
 assert(epub&&pos.spineIndex==2&&locked&&releases==1);++mappings;return {"xpath",0.25f};}};
std::string getStatsChapterTitle(const Epub&,int,std::optional<uint32_t>){return "chapter";}
struct KOReaderSyncActivity {
 bool pluginEventsOnly=false;SavedProgressPosition progress;bool returned=false,complete=false;unsigned updates=0,normalSync=0;
 enum State {SYNCING};State state=SYNCING;std::string statusMessage;GfxRenderer renderer;
 KOReaderSyncActivity()=default;
 KOReaderSyncActivity(GfxRenderer&,int&,const std::string& path,CrossPointPosition pos,SavedProgressPosition saved,std::string chapter,bool only)
 :pluginEventsOnly(only),progress(std::move(saved)){assert(locked&&path=="/book.epub"&&pos.spineIndex==2&&chapter=="chapter");phases.push_back(0);}
 ~KOReaderSyncActivity(){++destroyed;}
 void setLocalMappedProgress(SavedProgressPosition&& value){progress=std::move(value);}
 void returnToReader(){returned=true;}
 void completeAlreadySynced(){complete=true;}
 void requestUpdate(bool){++updates;}
 void performSync(){++normalSync;}
 void onWifiSelectionComplete(bool);
};
struct {std::unique_ptr<KOReaderSyncActivity> owner;
 void replaceActivity(std::unique_ptr<KOReaderSyncActivity> next){assert(locked&&releases==1);++replacements;phases.push_back(5);owner=std::move(next);}
} activityManager;
void* operator new(size_t bytes,const std::nothrow_t&) noexcept {++allocations;return allocationOk?std::malloc(bytes):nullptr;}
struct EpubReaderActivity {
 GfxRenderer renderer;int mappedInput=0;std::shared_ptr<Epub> epub=std::make_shared<Epub>();std::unique_ptr<Section> section=std::make_unique<Section>();
 int nextPageNumber=0,cachedChapterTotalPageCount=10,currentSpineIndex=2,footnoteDepth=1;
 bool pendingSyncSaveError=false,updated=false,stackSaved=false;
 void flushReaderSession(){++flushes;}
 CrossPointPosition getReadingPosition(){assert(epub&&section);return {};}
 bool saveProgress(int spine,int page,int pages){assert(epub&&section&&spine==2&&page==4&&pages==10);++saves;phases.push_back(1);return saveOk;}
 void requestUpdate(){updated=true;}
 void discardOverlayPage(){assert(locked);}
 bool isViewingNonlinearSpine(){return false;}
 void saveLinkStack(){assert(epub);stackSaved=true;}
 bool launchKOReaderSync();
};
struct {void setSleep(bool sleep){assert(!sleep);}} WiFi;
namespace pluginevents {int drains=0;void drain(GfxRenderer*){++drains;}}
""" + method(reader,"bool EpubReaderActivity::launchKOReaderSync(") + method(sync,"void KOReaderSyncActivity::onWifiSelectionComplete(") + r"""
void reset(){activityManager.owner.reset();allocations=saves=releases=mappings=replacements=flushes=destroyed=0;phases.clear();saveOk=allocationOk=true;ESP.free=128*1024;ESP.largest=96*1024;}
int main(){
 for(bool credentials:{false,true}){
  KOREADER_STORE.credentials=credentials;
  reset();{EpubReaderActivity reader;allocationOk=false;assert(!reader.launchKOReaderSync());assert(reader.epub&&reader.section&&!reader.stackSaved);assert(allocations==1&&saves==0&&releases==0&&replacements==0&&flushes==1);}
  reset();{EpubReaderActivity reader;ESP.largest=4096;assert(!reader.launchKOReaderSync());assert(reader.epub&&reader.section&&allocations==0&&saves==0&&releases==0);}
  reset();{EpubReaderActivity reader;saveOk=false;assert(reader.launchKOReaderSync());assert(reader.epub&&reader.section&&reader.pendingSyncSaveError&&reader.updated);assert(destroyed==1&&saves==1&&releases==0&&replacements==0);}
  reset();{EpubReaderActivity reader;assert(reader.launchKOReaderSync());assert(!reader.epub&&!reader.section&&reader.stackSaved);assert(saves==1&&releases==1&&replacements==1&&mappings==credentials);assert(activityManager.owner->pluginEventsOnly==!credentials);assert(activityManager.owner->progress.xpath==(credentials?"xpath":""));assert(phases.front()==0&&phases.back()==5);}
 }
 KOReaderSyncActivity plugin;plugin.pluginEventsOnly=true;pluginevents::drains=0;
 plugin.onWifiSelectionComplete(false);assert(plugin.returned&&!plugin.complete&&plugin.normalSync==0&&pluginevents::drains==0);
 plugin.returned=false;plugin.onWifiSelectionComplete(true);assert(!plugin.returned&&plugin.complete&&plugin.normalSync==0&&pluginevents::drains==1);
 KOReaderSyncActivity normal;normal.onWifiSelectionComplete(true);assert(normal.normalSync==1&&pluginevents::drains==1);
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"reader-sync.cpp";cpp.write_text(harness)
    binary=Path(directory)/"reader-sync"
    subprocess.run(shlex.split(os.environ.get("CXX","c++"))+["-std=c++20","-fno-exceptions","-Wall","-Wextra","-Werror","-I"+str(ROOT/"lib/Memory"),str(cpp),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Production reader sync: allocation/save failures preserve owner; full release precedes handoff; plugin-only WiFi cancellation/dispatch pass")
