#pragma once

#include <string>

namespace FsHelpers {
inline bool hasGifExtension(const std::string& path) { return path.ends_with(".gif"); }
}  // namespace FsHelpers
