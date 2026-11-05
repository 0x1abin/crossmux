#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
namespace WeReadClient {
// Full canonical fields are compared, not a hash or a rounded percentage alone.
// Missing timestamps are unsafe: the remote endpoint cannot establish a baseline.
template <typename Remote>
bool formatProgressBaseline(const Remote& remote, char* out, size_t capacity) {
  if (!remote.hasUpdateTime || remote.updateTime == 0 || !std::isfinite(remote.percent) || remote.percent < 0 ||
      remote.percent > 100)
    return false;
  const int n = snprintf(out, capacity, "v1 %u %u %u %u %u %.9g %s", remote.updateTime, remote.hasAppId ? 1u : 0u,
                         remote.appIdHash, remote.hasChapterOffset ? 1u : 0u, remote.chapterOffset,
                         static_cast<double>(remote.percent), remote.chapterUid[0] ? remote.chapterUid : "-");
  return n > 0 && static_cast<size_t>(n) < capacity;
}
}  // namespace WeReadClient
