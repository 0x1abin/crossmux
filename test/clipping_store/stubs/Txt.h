#pragma once
#include "HostBookCache.h"
struct Txt : HostBookCache {
  Txt(const std::string& p, const char*) : HostBookCache(p, "txt_") {}
};
