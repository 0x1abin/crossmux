#pragma once
#include "HostBookCache.h"
struct Xtc : HostBookCache {
  Xtc(const std::string& p, const char*) : HostBookCache(p, "xtc_") {}
};
