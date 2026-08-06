#pragma once

#include <string>

namespace FsHelpers {
inline bool hasSuffix(const std::string& path, const char* suffix) {
  const std::string ending(suffix);
  return path.size() >= ending.size() && path.compare(path.size() - ending.size(), ending.size(), ending) == 0;
}
inline bool hasEpubExtension(const std::string& path) { return hasSuffix(path, ".epub"); }
inline bool hasXtcExtension(const std::string& path) { return hasSuffix(path, ".xtc"); }
inline bool hasTxtExtension(const std::string& path) { return hasSuffix(path, ".txt"); }
inline bool hasMarkdownExtension(const std::string& path) { return hasSuffix(path, ".md"); }
}  // namespace FsHelpers
