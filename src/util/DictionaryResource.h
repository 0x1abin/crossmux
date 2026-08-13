#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace DictionaryResource {

constexpr uint8_t MANIFEST_VERSION = 1;
constexpr size_t MAX_ITEMS = 64;
constexpr size_t MAX_ID_BYTES = 31;
constexpr size_t MAX_FILE_BYTES = 100000000;

inline bool isValidId(const std::string_view id) {
  if (id.empty() || id.size() > MAX_ID_BYTES || !std::isalnum(static_cast<unsigned char>(id.front()))) return false;
  for (const unsigned char c : id) {
    if (!std::isalnum(c) && c != '_' && c != '-') return false;
  }
  return true;
}

inline bool isValidFileName(const std::string_view id, const std::string_view name) {
  if (!isValidId(id) || name.size() <= id.size() || name.substr(0, id.size()) != id) return false;
  const auto extension = name.substr(id.size());
  return extension == ".idx" || extension == ".dict" || extension == ".dict.dz" || extension == ".ifo";
}

inline int parseMarkerRevision(const char* marker) {
  if (!marker || marker[0] != '1' || marker[1] != ':') return -1;
  uint32_t revision = 0;
  const char* p = marker + 2;
  if (*p < '1' || *p > '9') return -1;
  while (*p >= '0' && *p <= '9') {
    revision = revision * 10 + static_cast<uint32_t>(*p++ - '0');
    if (revision > 0x7fffffffU) return -1;
  }
  return (*p == '\n' && p[1] == '\0') ? static_cast<int>(revision) : -1;
}

enum class LocalState : uint8_t { NotInstalled, Installed, UpdateAvailable, Conflict };

inline LocalState classify(const bool visibleExists, const bool hiddenExists, const int installedRevision,
                           const int catalogRevision) {
  if (visibleExists || (hiddenExists && installedRevision < 1)) return LocalState::Conflict;
  if (!hiddenExists) return LocalState::NotInstalled;
  return installedRevision < catalogRevision ? LocalState::UpdateAvailable : LocalState::Installed;
}

}  // namespace DictionaryResource
