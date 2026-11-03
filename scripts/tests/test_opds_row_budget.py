"""Compile real post-TLS OPDS row assembly against the reviewed SDK types."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SDK = Path(os.environ.get("CROSSMUX_TEST_SDK", ROOT / "freeink-sdk"))


def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <OpdsEntry.h>
#include <components/lists/list.h>
namespace fui=freeink::ui;
#include <Memory.h>
struct HalMemory {
 struct Heap {size_t freeBytes,largestBlockBytes;};
 inline static Heap heap{100000,50000};
 static Heap getDefaultHeap(){return heap;}
};
enum class StrId {STR_MEMORY_ERROR,STR_NO_SERVER_URL,STR_SET_CREDENTIALS_FIRST,STR_OPDS_AUTH_FAILED,
 STR_PARSE_FEED_FAILED,STR_NO_ENTRIES};
#define STR_OPDS_SHELF "shelf"
#define STR_OPDS_WISHLIST "wishlist"
#define STR_OPDS_HISTORY "history"
#define STR_OPDS_MY_ACCOUNT "account"
#define STR_OPDS_SEE_ALL "see all"
#define tr(x) (x)
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) (++truncationLogs)
inline int truncationLogs=0;
constexpr const char* STR_NO_ENTRIES="empty";
namespace UrlUtils {std::string buildUrl(const std::string& a,const std::string& b){return a+b;}}
StrId feedFailureMessage(){return StrId::STR_PARSE_FEED_FAILED;}
struct Feed {
 std::vector<OpdsEntry> rows,facets;
 std::string shelf,wishlist,history,empty;
 bool cut=false;
} feed;
struct OpdsFeedParser {
 std::vector<OpdsEntry> rows,facets;
 OpdsFeedParser():rows(std::move(feed.rows)),facets(std::move(feed.facets)){}
 std::vector<OpdsEntry> takeEntries(){return std::move(rows);}
 std::vector<OpdsEntry> takeFacetEntries(){return std::move(facets);}
 const std::string& getShelfUrl(){return feed.shelf;}
 const std::string& getWishlistUrl(){return feed.wishlist;}
 const std::string& getHistoryUrl(){return feed.history;}
 const std::string& getSearchTemplate(){return feed.empty;}
 const std::string& getSearchDescriptionUrl(){return feed.empty;}
 const std::string& getFeedTitle(){return feed.empty;}
 const std::string& getNextPageUrl(){return feed.empty;}
 const std::string& getPrevPageUrl(){return feed.empty;}
 const std::string& getFirstPageUrl(){return feed.empty;}
 const std::string& getLastPageUrl(){return feed.empty;}
 int currentPage(){return 1;} int pageCount(){return 1;} bool truncated(){return feed.cut;}
};
namespace freeink::opds {
struct OpdsClient {
 enum class FetchStatus {Ok,CredentialsMissing,AuthFailed,ParseFailed,Other};
 FetchStatus fetchFeed(const std::string&,OpdsFeedParser&){return FetchStatus::Ok;}
 bool tokensDirty(){return false;}
};
}
class OpdsBookBrowserActivity {
public:
 struct {std::string url="https://catalog/";} server;
 struct {void reset(){}} nav;
 freeink::opds::OpdsClient opdsClient;
 std::string currentPath,searchTemplate,searchDescriptionUrl,searchTemplateBase,feedTitle,
  pageNextHref,pagePrevHref,pageFirstHref,pageLastHref,errorMessage;
 std::vector<std::string> navigationHistory;
 int pageCurrent=0,pageTotal=0,updates=0;
 enum class State {ERROR,BROWSING};State state=State::ERROR;
 bool memoryError=false,routing=true;
 std::vector<OpdsEntry> entries;
 std::vector<fui::ListItem> rowItems;
 void fail(StrId s){memoryError=s==StrId::STR_MEMORY_ERROR;state=State::ERROR;}
 void persistTokens(){} void closeRouting(){routing=false;} void requestUpdate(){++updates;}
 void fetchFeed(const std::string&); bool rebuildRowItems(); void releaseEntries();
};
'''

MAIN = r'''
void setup(size_t capacity,size_t count,size_t facets,bool accounts) {
 feed={};truncationLogs=0;HalMemory::heap={100000,50000};
 feed.rows.reserve(capacity);
 for(size_t i=0;i<count;++i){OpdsEntry e;e.title="book"+std::to_string(i);feed.rows.push_back(std::move(e));}
 feed.facets.reserve(facets);
 for(size_t i=0;i<facets;++i){OpdsEntry e;e.title="facet"+std::to_string(i);feed.facets.push_back(std::move(e));}
 if(accounts){feed.shelf="shelf-url";feed.wishlist="wish-url";feed.history="history-url";}
}
int main(){
 {
  setup(8,6,20,true);const OpdsEntry* storage=feed.rows.data();
  OpdsBookBrowserActivity a;a.fetchFeed("");
  assert(a.entries.data()==storage && a.entries.capacity()==8 && a.entries.size()==8);
  assert(a.entries[6].title=="facet0" && a.entries[7].title=="facet1");
  assert(truncationLogs==1 && a.rowItems.size()==8 && !a.memoryError);
 }
 {
  setup(8,6,0,true);const OpdsEntry* storage=feed.rows.data();
  OpdsBookBrowserActivity a;a.fetchFeed("");
  assert(a.entries.data()==storage && a.entries.size()==8);
  assert(a.entries[6].title=="shelf" && a.entries[7].title=="wishlist");
  assert(a.entries[6].heading=="account" && a.entries[7].heading.empty());
  assert(truncationLogs==1);
 }
 {
  setup(64,0,0,true);OpdsBookBrowserActivity a;a.fetchFeed("");
  assert(a.entries.size()==3 && a.entries[2].title=="history" && truncationLogs==0);
 }
 {
  setup(64,61,10,true);OpdsBookBrowserActivity a;a.fetchFeed("");
  assert(a.entries.size()==OpdsLimits::MAX_ENTRIES && a.entries[61].title=="facet0");
  assert(truncationLogs==1);
 }
 // A legal small feed needs its row array + the SDK 4KiB reserve, not 32KiB.
 {
  setup(8,6,0,false);const size_t bytes=6*sizeof(fui::ListItem);
  HalMemory::heap={4096+bytes,4096+bytes};
  OpdsBookBrowserActivity a;a.fetchFeed("");
  assert(!a.memoryError && a.rowItems.size()==6);
 }
 // Account strings are refused before reserve when a large href won't fit.
 {
  setup(8,1,0,true);feed.shelf=std::string(9000,'x');
  const size_t bytes=3*sizeof(fui::ListItem);
  HalMemory::heap={4096+bytes,4096+bytes};
  OpdsBookBrowserActivity a;a.fetchFeed("");
  assert(!a.memoryError && a.entries.size()==3 && a.entries[1].title=="wishlist");
  assert(a.entries[1].heading=="account" && truncationLogs==1);
 }
 for(bool fragmented:{false,true}) {
  setup(8,6,0,false);const size_t bytes=6*sizeof(fui::ListItem);
  HalMemory::heap=fragmented?HalMemory::Heap{100000,4096+bytes-1}:HalMemory::Heap{4096+bytes-1,100000};
  OpdsBookBrowserActivity a;a.rowItems.reserve(2);a.rowItems.push_back(fui::ListItem{});a.fetchFeed("");
  assert(a.memoryError && a.state==OpdsBookBrowserActivity::State::ERROR && !a.routing);
  assert(a.entries.empty() && a.entries.capacity()==0 && a.rowItems.empty() && a.rowItems.capacity()==0);
 }
 std::puts("PASS real OPDS assembly: cap8, facets/accounts/order/MAX, exact4KiB headroom, strings, fragmentation/OOM and routing release");
}
'''


class OpdsRows(unittest.TestCase):
    def test_real_row_assembly(self):
        source = (ROOT / "src/activities/browser/OpdsBookBrowserActivity.cpp").read_text()
        helper = source[source.index("constexpr size_t OPDS_UI_FREE_RESERVE"):source.index("// License text")]
        methods = "\n".join(method(source, signature) for signature in (
            "void OpdsBookBrowserActivity::fetchFeed(",
            "bool OpdsBookBrowserActivity::rebuildRowItems(",
            "void OpdsBookBrowserActivity::releaseEntries(",
        ))
        with tempfile.TemporaryDirectory(prefix="opds-rows-") as directory:
            root = Path(directory)
            unit = root / "test.cpp"
            unit.write_text(HARNESS + helper + methods + MAIN)
            executable = root / "test"
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-fno-exceptions", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(ROOT / "lib/Memory"), "-I" + str(SDK / "libs/network/Opds/include"),
                            "-I" + str(SDK / "libs/ui/FreeInkUI/include"), str(unit), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
