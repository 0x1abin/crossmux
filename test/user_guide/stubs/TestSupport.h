#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// In-memory HAL with faults at the same boundaries as SD writes/reads/renames.
namespace guide_test {
inline std::map<std::string, std::vector<uint8_t>> files;
inline std::string failWrite;
inline std::string failRead;
inline std::string failRename;
inline std::string corruptOnFlush;
inline size_t writeLimit = SIZE_MAX;
inline size_t bytesWritten = 0;
inline bool failRecents = false;
}  // namespace guide_test

class HalFile {
 public:
  std::string path;
  size_t offset = 0;
  size_t fileSize() const { return guide_test::files.at(path).size(); }
  int read(void* output, size_t count) {
    if (path == guide_test::failRead) return -1;
    const auto& bytes = guide_test::files.at(path);
    count = std::min(count, bytes.size() - offset);
    memcpy(output, bytes.data() + offset, count);
    offset += count;
    return static_cast<int>(count);
  }
  size_t write(const uint8_t* data, size_t count) {
    if (path == guide_test::failWrite) {
      count = std::min(count, guide_test::writeLimit - guide_test::bytesWritten);
      guide_test::bytesWritten += count;
    }
    auto& bytes = guide_test::files[path];
    bytes.insert(bytes.end(), data, data + count);
    return count;
  }
  void flush() {
    if (path == guide_test::corruptOnFlush && !guide_test::files[path].empty()) guide_test::files[path][0] ^= 1;
  }
};

struct TestStorage {
  bool exists(const char* path) const { return guide_test::files.count(path) != 0; }
  bool remove(const char* path) { return guide_test::files.erase(path) != 0; }
  bool ensureDirectoryExists(const char*) { return true; }
  bool rename(const char* from, const char* to) {
    if (to == guide_test::failRename || !exists(from) || exists(to)) return false;
    guide_test::files[to] = std::move(guide_test::files[from]);
    return remove(from);
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (!exists(path) || path == guide_test::failRead) return false;
    file.path = path;
    file.offset = 0;
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    guide_test::files[path].clear();
    file.path = path;
    file.offset = 0;
    return true;
  }
};
inline TestStorage Storage;

struct RecentBook {
  std::string path, title, author, coverBmpPath;
};
class RecentBooksStore {
 public:
  std::vector<RecentBook> books;
  std::vector<RecentBook> persisted;
  static const char* getFilePath() { return "/.crosspoint/recent.json"; }
  static bool isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }
  const auto& getBooks() const { return books; }
  bool addBook(const std::string& path, const std::string& title, const std::string& author, const std::string& cover) {
    books.erase(std::remove_if(books.begin(), books.end(),
                               [&](const RecentBook& book) { return book.path == path || isMissing(book); }),
                books.end());
    books.insert(books.begin(), {path, title, author, cover});
    if (guide_test::failRecents) return false;
    persisted = books;
    guide_test::files[getFilePath()] = {'{', '}'};
    return true;
  }
};
inline RecentBooksStore RECENT_BOOKS;

class Epub {
  std::string path;

 public:
  Epub(const char* bookPath, const char*) : path(bookPath) {}
  std::string getThumbBmpPath() const { return "/.crosspoint/epub_" + path + "/thumb_[HEIGHT].bmp"; }
};

namespace HalSystem {
struct HeapInfo {
  uint32_t freeBytes, largestFreeBlockBytes;
};
inline HeapInfo getHeapInfo() { return {100000, 50000}; }
}  // namespace HalSystem
inline unsigned long millis() { return 0; }
inline void testLog(const char*, const char*, ...) {}
#define LOG_INF(...) testLog(__VA_ARGS__)
#define LOG_ERR(...) testLog(__VA_ARGS__)
