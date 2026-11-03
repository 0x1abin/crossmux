#pragma once
#include <map>
#include <string>
struct ReadingBookStats {
  std::string coverBmpPath;
};
struct HostStats {
  std::map<std::string, ReadingBookStats> books;
  int updates = 0;
  bool fail = false;
  const ReadingBookStats* findBook(const std::string& path) const {
    auto i = books.find(path);
    return i == books.end() ? nullptr : &i->second;
  }
  bool updateBookPath(const std::string& from, const std::string& to, const char*, const char*,
                      const std::string& cover) {
    ++updates;
    if (books.count(from)) {
      books[to] = books[from];
      books[to].coverBmpPath = cover;
      books.erase(from);
    }
    return !fail;
  }
  bool updateBookPathPrefix(const std::string&, const std::string&) {
    ++updates;
    return !fail;
  }
  bool removeBook(const std::string& path) {
    books.erase(path);
    return !fail;
  }
};
inline HostStats hostStats;
#define READING_STATS hostStats
