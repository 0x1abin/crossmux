#pragma once
#include <map>
#include <string>
struct HostRecent {
  std::map<std::string, std::string> books;
  int updates = 0;
  bool fail = false;
  bool updatePath(const std::string& from, const std::string& to, const std::string&, const std::string&) {
    ++updates;
    if (books.count(from)) {
      books[to] = books[from];
      books.erase(from);
    }
    return !fail;
  }
  bool updatePathPrefix(const std::string&, const std::string&) {
    ++updates;
    return !fail;
  }
  bool removeByPath(const std::string& path) { return books.erase(path) != 0; }
};
inline HostRecent hostRecent;
#define RECENT_BOOKS hostRecent
