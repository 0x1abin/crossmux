#pragma once

#include <string>

namespace BookCoverLoader {

enum class CachedCoverState { Missing, Ready, Terminal };

// Keeps a complete cover or an allowed empty marker; removes any other cached file and reports it Missing.
CachedCoverState checkCachedCover(const std::string& path, bool emptyIsTerminal);

std::string ensureThumbnail(const std::string& bookPath, int height, bool* generated = nullptr);
std::string ensureFullCover(const std::string& bookPath, std::string* title = nullptr, std::string* author = nullptr,
                            bool* generated = nullptr);

}  // namespace BookCoverLoader
