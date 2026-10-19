#!/usr/bin/env python3
"""Chapter tap zone: a TXT's named chapters are anchors inside one spine.

The zone action used to jump chapters by stepping currentSpineIndex, which is
only correct for EPUB. A TXT/MD book keeps every named chapter in a single
spine, so the step clamped straight back to the same chapter and the configured
zone looked dead — the toolbar's NextChapter had always used the TOC + pending
anchor instead. Both now share EpubReaderActivity::gotoChapterIndex().

This compiles the production getChapterTocIndex() / currentTocIndex() /
currentChapterIndex() / gotoChapterIndex() against injected EPUB, Section and
Txt collaborators, so the assertions run the real navigation rather than a
re-implementation, and then pins both dispatch sites to the shared helper.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
activity_cpp = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()


def method(text, signature):
    start = text.index(signature)
    start = text.rfind('\n', 0, start) + 1
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// --- injected collaborators -------------------------------------------------
struct GfxRenderer {};
struct RenderLock {
  RenderLock() {}
};

// The TXT chapter resolver: three named chapters at byte offsets 0/1000/2000 of
// the single spine, which is exactly what the converter produces for a TXT with
// three headings.
namespace Txt {
inline bool isTxtOrMd(const std::string& path) {
  return path.size() > 4 && path.compare(path.size() - 4, 4, ".txt") == 0;
}
inline int tocIndexForPosition(const std::string&, const std::string&, uint32_t offset, uint8_t* = nullptr) {
  return offset >= 2000 ? 2 : (offset >= 1000 ? 1 : 0);
}
}  // namespace Txt

struct TocItem {
  std::string title;
  std::string anchor;
  int spineIndex = 0;
};

struct Epub {
  std::string path = "/books/one.txt";
  std::string cachePath = "/.crosspoint/one";
  std::vector<TocItem> toc;
  int spines = 1;

  const std::string& getPath() const { return path; }
  const std::string& getCachePath() const { return cachePath; }
  int getTocItemsCount() const { return static_cast<int>(toc.size()); }
  TocItem getTocItem(int index) const { return toc[index]; }
  int getSpineItemsCount() const { return spines; }
  // A TXT's every named chapter maps back to spine 0; that is the whole point.
  int getTocIndexForSpineIndex(int) const { return 0; }
  int getSpineIndexForTocIndex(int index) const { return toc[index].spineIndex; }
};

struct Section {
  int currentPage = 0;
  Section(const std::shared_ptr<Epub>&, int, GfxRenderer&) {}
  int getTocIndexForPage(int) const { return 0; }
};

struct EpubReaderActivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  // mutable: currentTocIndex() is const but Section's constructor takes a
  // non-const renderer, exactly like the real activity.
  mutable GfxRenderer renderer;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::string pendingAnchor;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  int deferrals = 0;

  void clearDeferredReposition() { ++deferrals; }
  int currentTocIndex() const;
  int currentChapterIndex() const;
  bool gotoChapterIndex(int target);
};
'''

harness += method(activity_cpp, 'int getChapterTocIndex(const Epub& epub')
harness += '\n'
harness += method(activity_cpp, 'int EpubReaderActivity::currentTocIndex() const')
harness += '\n'
harness += method(activity_cpp, 'int EpubReaderActivity::currentChapterIndex() const')
harness += '\n'
harness += method(activity_cpp, 'bool EpubReaderActivity::gotoChapterIndex')

harness += r'''
int main() {
  // --- TXT: three named chapters, one spine -------------------------------
  {
    EpubReaderActivity reader;
    reader.epub = std::make_shared<Epub>();
    reader.epub->toc = {{"Chapter One", "ch1", 0}, {"Chapter Two", "ch2", 0}, {"Chapter Three", "ch3", 0}};
    reader.epub->spines = 1;
    reader.cachedVisibleTextOffset = 0;  // reading chapter one

    // The old zone action stepped the spine: clamp(0 + 1, 0, 1 - 1) = 0, i.e.
    // the tap returned "handled" while nothing moved. Pinned here so the
    // regression cannot come back unnoticed.
    const int oldTarget = std::clamp(reader.currentSpineIndex + 1, 0, reader.epub->getSpineItemsCount() - 1);
    assert(oldTarget == reader.currentSpineIndex);

    assert(reader.currentChapterIndex() == 0);
    assert(reader.gotoChapterIndex(reader.currentChapterIndex() + 1));
    assert(reader.pendingAnchor == "ch2");            // ... but the chapter moved
    assert(reader.deferrals == 1);
    assert(reader.section == nullptr);                // forced re-pagination

    // The reader now renders from the anchor, so the visible offset lands in
    // chapter two; the next tap steps to chapter three.
    reader.cachedVisibleTextOffset = 1000;
    assert(reader.currentChapterIndex() == 1);
    assert(reader.gotoChapterIndex(reader.currentChapterIndex() + 1));
    assert(reader.pendingAnchor == "ch3");
    assert(reader.deferrals == 2);

    // Last chapter: the tap must not move, must not re-paginate and must not
    // report a change (the caller skips the repaint on it).
    reader.cachedVisibleTextOffset = 2000;
    assert(reader.currentChapterIndex() == 2);
    assert(!reader.gotoChapterIndex(reader.currentChapterIndex() + 1));
    assert(reader.pendingAnchor == "ch3");
    assert(reader.deferrals == 2);

    // A TXT whose headings were never detected has no TOC: stepping must be a
    // no-op, not a clamp() over an empty range.
    EpubReaderActivity untoc;
    untoc.epub = std::make_shared<Epub>();
    untoc.cachedVisibleTextOffset = 0;
    assert(!untoc.gotoChapterIndex(1));
    assert(untoc.currentSpineIndex == 0);
    assert(untoc.deferrals == 0);
  }

  // --- EPUB: one chapter per spine, no anchor ------------------------------
  {
    EpubReaderActivity reader;
    reader.epub = std::make_shared<Epub>();
    reader.epub->path = "/books/one.epub";
    reader.epub->toc = {{"One", "", 0}, {"Two", "", 1}, {"Three", "", 2}};
    reader.epub->spines = 3;

    assert(reader.currentChapterIndex() == 0);
    assert(reader.gotoChapterIndex(reader.currentChapterIndex() + 1));
    assert(reader.currentSpineIndex == 1);
    assert(reader.pendingAnchor.empty());
    assert(reader.deferrals == 1);

    assert(reader.gotoChapterIndex(reader.currentChapterIndex() + 1));
    assert(reader.currentSpineIndex == 2);
    assert(!reader.gotoChapterIndex(reader.currentChapterIndex() + 1));  // past the end
    assert(reader.currentSpineIndex == 2);
    assert(reader.deferrals == 2);
  }

  std::puts("chapter zone navigation passed");
}
'''

with tempfile.TemporaryDirectory(prefix='tap-zone-chapter-') as tmp:
    cpp = Path(tmp) / 'check.cpp'
    binary = Path(tmp) / 'check'
    cpp.write_text(harness)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# --- both dispatch sites must use that one helper --------------------------
zone = method(activity_cpp, 'bool EpubReaderActivity::handleZoneShortAction')
chapter_case = zone[zone.index('case CrossPointSettings::TAP_ZONE_CHAPTER'):]
chapter_case = chapter_case[:chapter_case.index('case CrossPointSettings::TAP_ZONE_BOOKMARK')]
assert 'gotoChapterIndex(' in chapter_case, 'the Chapter zone no longer shares the toolbar navigation'
# Comments legitimately mention the spine step this replaces; only the code
# matters here.
chapter_code = re.sub(r'//[^\n]*', '', chapter_case)
assert 'currentSpineIndex' not in chapter_code, 'the Chapter zone steps the spine again and cannot advance a TXT'
assert 'pendingAnchor' not in chapter_code, 'the Chapter zone re-implements the anchor handling'

toolbar = activity_cpp[activity_cpp.index('const auto gotoChapter = '):]
toolbar = toolbar[:toolbar.index('};')]
assert 'gotoChapterIndex(' in toolbar, 'the toolbar next/prev/scrub chapter path bypasses the shared helper'

print('Chapter tap zone and toolbar share one TXT-aware navigation')
