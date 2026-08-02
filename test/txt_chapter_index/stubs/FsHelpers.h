#pragma once

#include <string_view>

namespace FsHelpers {

inline bool endsWith(const std::string_view value, const std::string_view suffix) {
  return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

inline bool hasTxtExtension(const std::string_view value) { return endsWith(value, ".txt"); }
inline bool hasBmpExtension(const std::string_view value) { return endsWith(value, ".bmp"); }
inline bool hasJpgExtension(const std::string_view value) {
  return endsWith(value, ".jpg") || endsWith(value, ".jpeg");
}

}  // namespace FsHelpers
