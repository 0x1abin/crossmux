"""Exercise production EPUB navigation and saved reading positions with small seams."""
from pathlib import Path
import unittest

from test_reading_ui_regressions import method, run_cpp


ROOT = Path(__file__).resolve().parents[2]


class EpubFootnoteNavigationTest(unittest.TestCase):
    def test_button_reader_uses_numbered_footnote_list_without_touch_link_storage(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        program = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#define LOG_ERR(...) ((void)0)
bool locked=false,failAllocation=false;
int allocations=0;
struct RenderLock {
 RenderLock() { assert(!locked);locked=true; }
 ~RenderLock() { locked=false; }
};
struct Footnote {std::string href;};
using FootnoteList=std::vector<Footnote>;
struct Page {};
struct Section {
 int currentPage=0,loads=0;bool missingPage=false;
 std::unique_ptr<Page> loadPage(int) {++loads;return missingPage?nullptr:std::make_unique<Page>();}
};
struct GfxRenderer {
 int margins=0;
 void getOrientedViewableTRBL(int* t,int* r,int* b,int* l) {++margins;*t=1;*r=2;*b=3;*l=4;}
};
struct MappedInputManager {bool touch=false;bool hasTouch() const {return touch;}};
struct {int screenMargin=5;} SETTINGS;
struct {void resumeSession() {}} READING_STATS;
struct Activity {virtual ~Activity()=default;};
struct EpubReaderFootnotesActivity : Activity {
 const FootnoteList& footnotes;
 EpubReaderFootnotesActivity(GfxRenderer&,MappedInputManager&,const FootnoteList& notes):footnotes(notes) {
  assert(locked);
 }
};
struct EpubReaderFootnoteSelectActivity : Activity {
 EpubReaderFootnoteSelectActivity(GfxRenderer&,MappedInputManager&,std::unique_ptr<Page> page,int left,int top) {
  assert(locked && page && left==9 && top==6);
 }
};
template<class T,class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
 ++allocations;
 return failAllocation ? nullptr : std::make_unique<T>(std::forward<Args>(args)...);
}
struct FootnoteResult {std::string href;};
struct ActivityResult {bool isCancelled=false;std::variant<FootnoteResult> data;};
struct EpubReaderActivity {
 std::unique_ptr<Section> section=std::make_unique<Section>();
 FootnoteList currentPageFootnotes={{"notes.xhtml#1"},{"notes.xhtml#2"}};
 GfxRenderer renderer;MappedInputManager mappedInput;
 std::unique_ptr<Activity> child;
 std::function<void(const ActivityResult&)> handler;
 std::string navigated;bool savedPosition=false;
 int updates=0,menus=0;
 void navigateToHref(const std::string& href,bool save) {navigated=href;savedPosition=save;}
 void requestUpdate() {++updates;}
 void openReaderMenu() {++menus;}
 void startActivityForResult(std::unique_ptr<Activity>&& activity,std::function<void(const ActivityResult&)> callback) {
  assert(locked);child=std::move(activity);handler=std::move(callback);
 }
 void openFootnoteSelect(bool);
};
''' + method(source, 'void EpubReaderActivity::openFootnoteSelect(') + r'''
int main() {
 for(bool touch:{false,true}) {
  EpubReaderActivity reader;reader.mappedInput.touch=touch;
  allocations=0;reader.openFootnoteSelect(false);
  assert(!locked && allocations==1 && reader.child);
  assert(reader.section->loads==int(touch) && reader.renderer.margins==int(touch));
  auto* list=dynamic_cast<EpubReaderFootnotesActivity*>(reader.child.get());
  assert(touch ? list==nullptr : list!=nullptr);
  if(list)assert(&list->footnotes==&reader.currentPageFootnotes && list->footnotes.size()==2);
  ActivityResult result;result.data=FootnoteResult{"notes.xhtml#2"};reader.handler(result);
  assert(reader.navigated=="notes.xhtml#2" && reader.savedPosition && reader.updates==1);
 }
 for(bool reopen:{false,true}) {
  EpubReaderActivity reader;reader.openFootnoteSelect(reopen);
  ActivityResult result;result.isCancelled=true;reader.handler(result);
  assert(reader.menus==int(reopen) && reader.updates==int(!reopen) && reader.navigated.empty());
 }
 EpubReaderActivity single;single.currentPageFootnotes.resize(1);allocations=0;
 single.openFootnoteSelect(false);
 assert(single.navigated=="notes.xhtml#1" && single.savedPosition && !single.child && allocations==0);
 EpubReaderActivity oom;failAllocation=true;oom.openFootnoteSelect(false);failAllocation=false;
 assert(!locked && !oom.child && oom.section->loads==0 && oom.currentPageFootnotes.size()==2);
 EpubReaderActivity missing;missing.mappedInput.touch=true;missing.section->missingPage=true;
 allocations=0;missing.openFootnoteSelect(false);assert(!locked && !missing.child && allocations==0);
}
'''
        run_cpp(program)

    def test_nonlinear_notes_keep_reading_position_and_skip_normal_navigation(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        header = (ROOT / 'src/activities/reader/EpubReaderActivity.h').read_text()
        contents_start = source.index('      const auto item = epub->getTocItem(panelIndex);',
                                      source.index('const auto activateRow'))
        contents = source[contents_start:source.index('\n      overlay =', contents_start)]
        program = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "CrossPointPosition.h"
#include "ChapterPosition.h"
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
unsigned long millis() { return 100; }
struct RenderLock { RenderLock() {} };
namespace Txt { bool isTxtOrMd(const std::string&) { return false; } }
struct Epub {
 struct Item { bool linear=true; };
 std::vector<Item> items={{true},{false},{true},{false},{true},{false}};
 int progressLookups=0;
 int getSpineItemsCount() const { return items.size(); }
 Item getSpineItem(int index) const { return items.at(index); }
 bool isLinearSpineItem(int index) const { return index>=0 && index<static_cast<int>(items.size()) && items[index].linear; }
 int getLinearSpineItemsCount() const { return 3; }
 int getSpineIndexForLinearIndex(int index) const { return index>=0 && index<3 ? index*2 : -1; }
 int getLinearIndexForSpineIndex(int index) const { return index>=0 && index<6 && index%2==0 ? index/2 : -1; }
 int getNextLinearSpineIndex(int index) const {
  for(++index;index<6;++index) if(items[index].linear)return index;
  return -1;
 }
 int getPreviousLinearSpineIndex(int index) const {
  for(--index;index>=0;--index) if(items[index].linear)return index;
  return -1;
 }
 int resolveHrefToSpineIndex(const std::string& href) const {
  return href.starts_with("doc") && href.size()>3 && href[3]>='0' && href[3]<='5' ? href[3]-'0' : -1;
 }
 bool findSpineForProgress(float value,int& spine,float& chapter) {
  if(!std::isfinite(value))return false;
  ++progressLookups;
  spine=value>=1 ? 4 : (value>=0.5f ? 2 : 0);chapter=value>=1 ? 1 : 0.5f;return true;
 }
 int getBookSize() const { return 3000; }
 float calculateProgress(int spine,float fraction) const { return (spine/2+fraction)/3; }
 std::string getPath() const { return "/book.epub"; }
 struct Toc { std::string title="body";std::string anchor;int spineIndex=0; };
 int getTocItemsCount() const { return 3; }
 Toc getTocItem(int index) const { return {"body", "",index*2}; }
};
struct Section {
 int currentPage=4,pageCount=10;bool building=false;
 int estimatedTotalPages() const { return pageCount; }
 bool isBuilding() const { return building; }
 std::optional<uint16_t> getParagraphIndexForPage(uint16_t) const { return std::nullopt; }
 std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t) const { return 4242; }
 std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t) const { return std::nullopt; }
};
struct ProgressChangeResult {
 bool hasVisibleTextOffset=false,hasSavedProgress=false;
 int spineIndex=0,page=0,totalPages=0;
 uint32_t visibleTextOffset=0;std::string xpath;float percentage=0;
};
struct SavedProgressPosition { std::string xpath;float percentage; };
namespace ProgressMapper {
 int mappedSpineIndex=2;
 CrossPointPosition toCrossPoint(const std::shared_ptr<Epub>&,const SavedProgressPosition&,int,int,int) {
  return {mappedSpineIndex,5,10};
 }
}
struct Stats {
 int updates=0;std::string title;uint8_t percent=0;
 void noteActivity() {}
 void updateProgress(uint8_t p,bool,const std::string& text,uint8_t) { ++updates;percent=p;title=text; }
} READING_STATS;
std::string getStatsChapterTitle(const Epub&,int spine,std::optional<uint32_t>,uint8_t* =nullptr) {
 return "body"+std::to_string(spine);
}
uint8_t getStatsChapterProgressPercent(int page,int total) { return total>0 ? page*100/total : 0; }
int clampPercent(int value) { return std::clamp(value,0,100); }
namespace EpubReaderUtils {
 CrossPointPosition persisted{};int saves=0;
 bool saveProgress(const Epub&,int spine,int page,int count,std::optional<uint32_t> offset) {
  persisted={spine,page,count};persisted.hasVisibleTextOffset=offset.has_value();
  persisted.visibleTextOffset=offset.value_or(0);++saves;return true;
 }
}
struct EpubReaderActivity {
 std::shared_ptr<Epub> epub=std::make_shared<Epub>();
 std::unique_ptr<Section> section=std::make_unique<Section>();
 int currentSpineIndex=0,nextPageNumber=0,cachedChapterTotalPageCount=10;
 int renderedSpineIndex_=0,committedPageNumber_=4;
 std::optional<uint32_t> currentPageVisibleOffset=4242,cachedVisibleTextOffset,pendingOffsetJump;
 std::optional<uint16_t> pendingPageJump;
 std::string pendingAnchor;
 bool pendingPercentJump=false,legacyProgressPending=false;
 float pendingSpineProgress=0;
 unsigned long lastPageTurnTime=0;
 int footnoteDepth=0;
 int renderer=0;
 static constexpr int MAX_FOOTNOTE_DEPTH=3;
''' + method(header, '  struct SavedPosition {') + r''';
 SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH]{};
 void clearDeferredReposition() {cachedChapterTotalPageCount=0;cachedVisibleTextOffset.reset();}
 void requestUpdate() {}
 int currentTocIndex() const { return 0; }
 bool isViewingNonlinearSpine() const;
 bool pushSavedPosition();
 void leaveNonlinearSpine(int);
 CrossPointPosition getReadingPosition() const;
 CrossPointPosition getCurrentPosition() const;
 void navigateToHref(const std::string&,bool);
 void restoreSavedPosition();
 bool pageTurn(bool);
 bool skipPages(int);
 void onReturnFromEndOfBook();
 int currentChapterIndex() const;
 bool gotoChapterIndex(int);
 bool jumpToFraction(float);
 bool saveProgress(int,int,int);
 void applyProgressChange(const ProgressChangeResult&);
 float bookProgressFor(const ChapterPosition&,bool=false) const;
 void activateContents(int panelIndex) {
''' + contents + r'''
 }
};
''' + '\n'.join(method(source, signature) for signature in (
            'bool EpubReaderActivity::isViewingNonlinearSpine(',
            'bool EpubReaderActivity::pushSavedPosition(',
            'void EpubReaderActivity::leaveNonlinearSpine(',
            'CrossPointPosition EpubReaderActivity::getReadingPosition(',
            'CrossPointPosition EpubReaderActivity::getCurrentPosition(',
            'void EpubReaderActivity::navigateToHref(',
            'void EpubReaderActivity::restoreSavedPosition(',
            'bool EpubReaderActivity::pageTurn(',
            'bool EpubReaderActivity::skipPages(',
            'void EpubReaderActivity::onReturnFromEndOfBook(',
            'int EpubReaderActivity::currentChapterIndex(',
            'bool EpubReaderActivity::gotoChapterIndex(',
            'bool EpubReaderActivity::jumpToFraction(',
            'bool EpubReaderActivity::saveProgress(',
            'void EpubReaderActivity::applyProgressChange(',
            'float EpubReaderActivity::bookProgressFor(',
        )) + r'''
void show(EpubReaderActivity& reader,int spine,int page=0,int pages=3) {
 reader.currentSpineIndex=spine;reader.section=std::make_unique<Section>();
 reader.section->currentPage=page;reader.section->pageCount=pages;
 reader.renderedSpineIndex_=spine;reader.committedPageNumber_=page;
}
int main() {
 EpubReaderActivity reader;
 const float progress=reader.bookProgressFor({4,10});
 const float displayedProgress=reader.bookProgressFor({4,10},true);
 reader.navigateToHref("missing.xhtml#note",true);
 assert(reader.footnoteDepth==0 && reader.currentSpineIndex==0);
 reader.section->currentPage=5;
 reader.navigateToHref("doc1.xhtml#note",true);
 assert(reader.footnoteDepth==0 && reader.currentSpineIndex==0);
 assert(!reader.saveProgress(0,5,10));
 reader.section->currentPage=4;
 reader.navigateToHref("doc1.xhtml#note",true);
 assert(reader.footnoteDepth==1 && reader.currentSpineIndex==1);
 assert(reader.savedPositions[0].pageNumber==4 && reader.savedPositions[0].pageCount==10);
 assert(reader.savedPositions[0].visibleTextOffset==4242);
 show(reader,1,1,3);
 assert(reader.getCurrentPosition().spineIndex==1);
 auto body=reader.getReadingPosition();
 assert(body.spineIndex==0 && body.pageNumber==4 && body.totalPages==10 && body.visibleTextOffset==4242);
 assert(reader.bookProgressFor({1,3})==progress);
 assert(reader.bookProgressFor({1,3},true)==displayedProgress);
 assert(!reader.saveProgress(1,2,3));
 assert(reader.saveProgress(1,1,3));
 assert(EpubReaderUtils::persisted.spineIndex==0 && EpubReaderUtils::persisted.pageNumber==4);
 assert(EpubReaderUtils::persisted.totalPages==10 && EpubReaderUtils::persisted.visibleTextOffset==4242);
 assert(READING_STATS.title=="body0");
 reader.restoreSavedPosition();
 assert(reader.footnoteDepth==0 && reader.currentSpineIndex==0 && reader.nextPageNumber==4);
 assert(reader.pendingOffsetJump==4242);

 // A picker may release the committed section while retaining its position.
 reader=EpubReaderActivity{};reader.section.reset();reader.nextPageNumber=7;
 reader.cachedChapterTotalPageCount=22;reader.cachedVisibleTextOffset=1777;
 reader.navigateToHref("doc1.xhtml#note",true);show(reader,1);
 body=reader.getReadingPosition();
 assert(body.spineIndex==0 && body.pageNumber==7 && body.totalPages==22 && body.visibleTextOffset==1777);

 // Normal reading skips auxiliary spines, including reverse and the end menu.
 show(reader,0,2);assert(reader.pageTurn(true) && reader.currentSpineIndex==2);
 show(reader,2);assert(reader.pageTurn(false) && reader.currentSpineIndex==0);
 show(reader,0);assert(reader.skipPages(1) && reader.currentSpineIndex==2);
 show(reader,2);assert(reader.skipPages(-1) && reader.currentSpineIndex==0);
 show(reader,4,2);assert(reader.pageTurn(true) && reader.currentSpineIndex==6);
 reader.onReturnFromEndOfBook();assert(reader.currentSpineIndex==4);
 show(reader,4);assert(reader.skipPages(1) && reader.currentSpineIndex==6);

 // Note pagination stays in the file; either boundary and fast skip return.
 for(int action=0;action<3;++action) {
  reader=EpubReaderActivity{};
  reader.navigateToHref("doc1.xhtml#note",true);
  show(reader,1,action==0?2:0);
  assert(action==2 ? reader.skipPages(1) : reader.pageTurn(action==0));
  assert(reader.currentSpineIndex==0 && reader.nextPageNumber==4 && reader.footnoteDepth==0);
 }
 reader=EpubReaderActivity{};reader.navigateToHref("doc1.xhtml#note",true);show(reader,1);
 assert(reader.pageTurn(true) && reader.currentSpineIndex==1 && reader.section->currentPage==1);
 reader.navigateToHref("doc0.xhtml#wr-ref-1",true);
 assert(reader.currentSpineIndex==1 && reader.footnoteDepth==1);
 show(reader,1,1);
 reader.navigateToHref("doc0.xhtml#wr-ref-1",true);
 assert(reader.currentSpineIndex==0 && reader.footnoteDepth==0 && reader.pendingAnchor=="wr-ref-1");

 // The nearest linear origin wins, and a full nested stack retains that origin.
 reader=EpubReaderActivity{};reader.navigateToHref("doc2.xhtml#body",true);show(reader,2,5,20);
 reader.navigateToHref("doc3.xhtml#note",true);show(reader,3);
 body=reader.getReadingPosition();assert(body.spineIndex==2 && body.pageNumber==5 && body.totalPages==20);
 for(int i=0;i<5;++i) {reader.navigateToHref("#next",true);show(reader,3);}
 body=reader.getReadingPosition();assert(body.spineIndex==2 && body.pageNumber==5 && body.totalPages==20);
 reader=EpubReaderActivity{};reader.navigateToHref("doc1.xhtml#a",true);show(reader,1);
 for(int i=0;i<5;++i) {reader.navigateToHref("#next",true);show(reader,1);}
 assert(reader.footnoteDepth==3 && reader.getReadingPosition().spineIndex==0);

 // Chapter ordinal and percentage navigation exclude notes and end the visit.
 reader=EpubReaderActivity{};show(reader,2);assert(reader.currentChapterIndex()==1);
 assert(reader.gotoChapterIndex(2) && reader.currentSpineIndex==4);
 assert(!reader.gotoChapterIndex(3));
 reader=EpubReaderActivity{};reader.navigateToHref("doc1.xhtml#note",true);show(reader,1);
 assert(reader.currentChapterIndex()==0 && reader.gotoChapterIndex(0));
 assert(reader.currentSpineIndex==0 && reader.footnoteDepth==0);
 assert(reader.jumpToFraction(1) && reader.currentSpineIndex==4 && reader.pendingSpineProgress==1);
 assert(reader.epub->progressLookups==1);
 assert(!reader.jumpToFraction(std::numeric_limits<float>::quiet_NaN()));

 // Explicit Contents/bookmark/clipping navigation ends only a note visit.
 for(int path=0;path<4;++path) {
  reader=EpubReaderActivity{};reader.navigateToHref("doc1.xhtml#note",true);show(reader,1);
  reader.pendingAnchor="old";reader.pendingOffsetJump=99;reader.pendingPageJump=7;reader.pendingPercentJump=true;
  ProgressChangeResult change;change.spineIndex=path==1 ? 4 : 2;change.page=3;
  change.hasVisibleTextOffset=path==0;change.visibleTextOffset=8000;change.hasSavedProgress=path==2;
  if(path==3)reader.activateContents(1);else reader.applyProgressChange(change);
  assert(reader.currentSpineIndex==(path==1 ? 4 : 2) && reader.footnoteDepth==0);
  assert(reader.pendingAnchor.empty() && !reader.pendingPageJump && !reader.pendingPercentJump);
  assert(path==0 ? reader.pendingOffsetJump==8000 : !reader.pendingOffsetJump);
 }
 reader=EpubReaderActivity{};reader.navigateToHref("doc2.xhtml#body",true);show(reader,2);
 reader.activateContents(2);assert(reader.footnoteDepth==1 && reader.currentSpineIndex==4);
 ProgressChangeResult change;change.spineIndex=0;reader.applyProgressChange(change);
 assert(reader.footnoteDepth==1 && reader.currentSpineIndex==0);

 // Bookmark/clipping jumps into notes retain the latest body, before clearing cached position.
 for(int path=0;path<3;++path) for(bool released:{false,true}) {
  reader=EpubReaderActivity{};reader.navigateToHref("doc2.xhtml#body",true);show(reader,2,6,18);
  reader.currentPageVisibleOffset=6868;
  if(released) {
   reader.section.reset();reader.nextPageNumber=6;
   reader.cachedChapterTotalPageCount=18;reader.cachedVisibleTextOffset=6868;
  }
  ProgressChangeResult note;note.spineIndex=path==2 ? 4 : 3;note.page=1;
  note.hasVisibleTextOffset=path==0;note.visibleTextOffset=9000;note.hasSavedProgress=path==2;
  ProgressMapper::mappedSpineIndex=3;reader.applyProgressChange(note);ProgressMapper::mappedSpineIndex=2;
  assert(reader.currentSpineIndex==3 && reader.footnoteDepth==2);
  assert(reader.savedPositions[1].spineIndex==2 && reader.savedPositions[1].pageNumber==6);
  assert(reader.savedPositions[1].pageCount==18 && reader.savedPositions[1].visibleTextOffset==6868);
  show(reader,3,2,3);body=reader.getReadingPosition();
  assert(body.spineIndex==2 && body.pageNumber==6 && body.totalPages==18 && body.visibleTextOffset==6868);
  assert(reader.saveProgress(3,2,3));
  assert(EpubReaderUtils::persisted.spineIndex==2 && EpubReaderUtils::persisted.pageNumber==6);
  assert(EpubReaderUtils::persisted.totalPages==18 && EpubReaderUtils::persisted.visibleTextOffset==6868);
  assert(reader.pageTurn(true) && reader.currentSpineIndex==2 && reader.nextPageNumber==6);
  assert(reader.pendingOffsetJump==6868 && reader.footnoteDepth==1);
 }
 // A pending, uncommitted body page cannot become the persisted return origin.
 for(bool exactOffset:{false,true}) {
  reader=EpubReaderActivity{};reader.section->currentPage=5;
  ProgressChangeResult note;note.spineIndex=1;note.hasVisibleTextOffset=exactOffset;
  reader.applyProgressChange(note);
  assert(reader.currentSpineIndex==0 && reader.footnoteDepth==0 && reader.section->currentPage==5);
 }
 // Repositioning within or between notes keeps the existing body frame once.
 reader=EpubReaderActivity{};reader.navigateToHref("doc3.xhtml#note",true);show(reader,3);
 change.spineIndex=5;reader.applyProgressChange(change);
 assert(reader.currentSpineIndex==5 && reader.footnoteDepth==1 && reader.getReadingPosition().spineIndex==0);

 // An auxiliary document opened without a return frame falls back safely.
 reader=EpubReaderActivity{};show(reader,3);reader.restoreSavedPosition();
 assert(reader.currentSpineIndex==0 && reader.nextPageNumber==0);
}
'''
        run_cpp(program, include_dirs=(ROOT / 'lib/KOReaderSync', ROOT / 'src/activities/reader'))

    def test_sync_uses_body_position_and_leaves_note_visit_transient(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        for signature in ('bool EpubReaderActivity::launchKOReaderSync(',
                          'bool EpubReaderActivity::launchWeReadSync('):
            sync = method(source, signature)
            self.assertIn('getReadingPosition()', sync)
            self.assertNotIn('getCurrentPosition()', sync)
        ko = method(source, 'bool EpubReaderActivity::launchKOReaderSync(')
        self.assertIn('footnoteDepth > 0 && !isViewingNonlinearSpine()', ko)


if __name__ == '__main__':
    unittest.main()
