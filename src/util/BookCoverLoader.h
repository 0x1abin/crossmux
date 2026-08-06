#pragma once

#include <string>

namespace BookCoverLoader {

std::string ensureThumbnail(const std::string& bookPath, int height);
std::string ensureFullCover(const std::string& bookPath, std::string* title = nullptr, std::string* author = nullptr);

}  // namespace BookCoverLoader
