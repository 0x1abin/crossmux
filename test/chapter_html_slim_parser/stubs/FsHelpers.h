#pragma once

#include <string>

namespace FsHelpers {
inline bool hasTxtExtension(const std::string& path) { return path.ends_with(".txt"); }
inline bool hasMarkdownExtension(const std::string& path) { return path.ends_with(".md"); }
inline std::string decodeUriEscapes(const std::string& path) { return path; }
inline std::string normalisePath(const std::string& path) { return path; }
}  // namespace FsHelpers
