"""Exercise production cache invalidation and packaged-offset mapping with small I/O stubs."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile


def function(path, signature):
    source = path.read_text()
    begin = source.index(signature)
    end = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


repo = Path(__file__).resolve().parents[2]
methods = function(repo / "src/util/BookCacheUtils.cpp", "bool clearBookDerivedCache(")
methods += "\n" + function(
    repo / "src/activities/apps/weread/webapi/WeReadProgressSyncActivity.cpp", "bool mapPackagedOffset("
)

prelude = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
namespace fs = std::filesystem;
std::string cache;
std::string readText(const std::string& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void put(const std::string& path, const std::string& text = "saved") {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path) << text;
}
struct {
  std::string rejectRemove, rejectRemoveDir;
  bool exists(const char* path) const { return fs::exists(path); }
  bool remove(const char* path) const {
    if (path == rejectRemove) return false;
    std::error_code error;
    return fs::remove(path, error) && !error;
  }
  bool removeDir(const char* path) const {
    if (path == rejectRemoveDir) return false;
    std::error_code error;
    fs::remove_all(path, error);
    return !error;
  }
} Storage;
namespace FsHelpers {
bool hasReflowableBookExtension(const std::string& path) { return path.ends_with(".epub"); }
}
std::string bookCachePath(const std::string&) { return cache; }
enum class OffsetDirection { VisibleToNative, NativeToVisible };
struct Epub {
  mutable int extractions = 0;
  bool extractOk = true;
  std::string getCachePath() const { return cache; }
  struct Item { std::string href; };
  Item getSpineItem(int index) const {
    assert(index == 1);
    return {"OEBPS/ch000001.xhtml"};
  }
  bool extractItemToFile(const std::string& href, const std::string& path) const {
    ++extractions;
    assert(href == "OEBPS/ch000001.xhtml");
    put(path, extractOk ? "current EPUB chapter" : "partial extraction");
    // The production extraction API removes its incomplete output on failure.
    if (!extractOk) fs::remove(path);
    return extractOk;
  }
};
namespace WeReadXhtmlCodec {
int visibleCalls = 0, nativeCalls = 0;
bool mapOk = true;
bool visibleToNativeOffset(const std::string& path, uint32_t input, uint32_t& output) {
  ++visibleCalls;
  assert(readText(path) == "current EPUB chapter");
  if (mapOk) output = input + 100;
  return mapOk;
}
bool nativeToVisibleOffset(const std::string& path, uint32_t input, uint32_t& output) {
  ++nativeCalls;
  assert(readText(path) == "current EPUB chapter");
  if (mapOk) output = input - 100;
  return mapOk;
}
}
'''

checks = r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  cache = std::string(argv[1]) + "/cache";
  const char* preserved[] = {"progress.bin", "progress.bin.tmp", "progress.bin.bak", "links.bin",
                             "cover.override", "cover.bmp", "thumb_120.bmp", "unrelated-user-data"};
  const char* derived[] = {"book.bin", "css_rules.cache", "css_rules.cache.tmp", "css_rules.cache.bak",
                           "spine.bin.tmp", "toc.bin.tmp", ".items.bin"};
  for (const char* name : preserved) put(cache + "/" + name, name);
  put(std::string(argv[1]) + "/bookmarks/book.json", "bookmark");
  put(std::string(argv[1]) + "/clippings/book.bin", "highlight");
  for (const char* name : derived) put(cache + "/" + name);
  put(cache + "/sections/nested/1.bin");
  assert(clearBookDerivedCache("/book.epub"));
  assert(!fs::exists(cache + "/sections"));
  for (const char* name : derived) assert(!fs::exists(cache + "/" + name));
  for (const char* name : preserved) assert(readText(cache + "/" + name) == name);
  assert(readText(std::string(argv[1]) + "/bookmarks/book.json") == "bookmark");
  assert(readText(std::string(argv[1]) + "/clippings/book.bin") == "highlight");
  assert(clearBookDerivedCache("/book.epub"));
  put(cache + "/sections/1.bin");
  put(cache + "/book.bin");
  assert(clearBookDerivedCache("/ordinary.png"));
  assert(fs::exists(cache + "/sections/1.bin") && fs::exists(cache + "/book.bin"));
  Storage.rejectRemoveDir = cache + "/sections";
  assert(!clearBookDerivedCache("/book.epub"));
  assert(fs::exists(cache + "/sections/1.bin") && fs::exists(cache + "/book.bin"));
  Storage.rejectRemoveDir.clear();
  Storage.rejectRemove = cache + "/book.bin";
  assert(!clearBookDerivedCache("/book.epub"));
  assert(!fs::exists(cache + "/sections") && fs::exists(cache + "/book.bin"));
  for (const char* name : preserved) assert(readText(cache + "/" + name) == name);
  Storage.rejectRemove.clear();
  assert(clearBookDerivedCache("/book.epub"));

  Epub epub;
  const std::string temporary = cache + "/weread-progress-map.part";
  const std::string loose = cache + "/chapters/000001.xhtml";
  put(loose, "newer loose chapter which must never be mapped");
  put(temporary, "interrupted extraction");
  uint32_t output = 0;
  assert(mapPackagedOffset(epub, 1, OffsetDirection::VisibleToNative, 17, output) && output == 117);
  assert(!fs::exists(temporary));
  assert(mapPackagedOffset(epub, 1, OffsetDirection::NativeToVisible, 117, output) && output == 17);
  assert(!fs::exists(temporary));
  assert(WeReadXhtmlCodec::visibleCalls == 1 && WeReadXhtmlCodec::nativeCalls == 1);
  WeReadXhtmlCodec::mapOk = false;
  output = UINT32_MAX;
  assert(!mapPackagedOffset(epub, 1, OffsetDirection::NativeToVisible, 117, output));
  assert(output == UINT32_MAX && !fs::exists(temporary));
  assert(WeReadXhtmlCodec::nativeCalls == 2);
  WeReadXhtmlCodec::mapOk = true;
  epub.extractOk = false;
  assert(!mapPackagedOffset(epub, 1, OffsetDirection::VisibleToNative, 17, output));
  assert(output == UINT32_MAX && !fs::exists(temporary));
  assert(WeReadXhtmlCodec::visibleCalls == 1);  // No fallback to the loose chapter.
  epub.extractOk = true;
  put(temporary, "interrupted extraction");
  Storage.rejectRemove = temporary;
  const int calls = epub.extractions;
  assert(!mapPackagedOffset(epub, 1, OffsetDirection::VisibleToNative, 17, output));
  assert(epub.extractions == calls && readText(temporary) == "interrupted extraction");
  fs::remove(temporary);
  assert(!mapPackagedOffset(epub, 1, OffsetDirection::VisibleToNative, 17, output));
  assert(fs::exists(temporary));  // Cleanup failure is reported even after a successful mapping.
  Storage.rejectRemove.clear();
  fs::remove(temporary);
  assert(readText(loose) == "newer loose chapter which must never be mapped");
}
'''

with tempfile.TemporaryDirectory(prefix="weread-cache-sync-") as directory:
    work = Path(directory)
    source = work / "check.cpp"
    source.write_text(prelude + methods + checks)
    binary = work / "check"
    environment = dict(os.environ, CCACHE_DISABLE="1")
    subprocess.run(
        [sys.argv[1], "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
        check=True,
        env=environment,
    )
    subprocess.run([str(binary), str(work / "sd")], check=True)
