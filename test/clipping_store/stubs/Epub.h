#pragma once
#include "HostBookCache.h"
struct Epub : HostBookCache {
  Epub(const std::string& p, const char*) : HostBookCache(p, "epub_") {}
};
