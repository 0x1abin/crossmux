#pragma once

#include "CrossPointSettings.h"

namespace CrossMuxEndpoints {

inline constexpr char GLOBAL_HOST[] = "crossmux.com";
inline constexpr char CHINA_HOST[] = "crossmux.cn";
inline constexpr char OTA_MANIFEST_FORMAT[] = "https://%s/api/ota/manifest?variant=%s%s&model=%s";
inline constexpr char FONT_MANIFEST_FORMAT[] = "https://%s/api/assets/fonts/m%s-b%s/fonts.json";
inline constexpr char DICTIONARY_MANIFEST_FORMAT[] = "https://%s/api/assets/dictionaries/manifest?version=1&lang=%s";
inline constexpr char AIRPAGE_SUBDOMAIN[] = "airpage.";

constexpr const char* hostFor(const CrossPointSettings::ContentProfile profile) {
  switch (profile) {
    case CrossPointSettings::ContentProfile::Global:
      return GLOBAL_HOST;
    case CrossPointSettings::ContentProfile::China:
      return CHINA_HOST;
  }
  return GLOBAL_HOST;
}

constexpr const char* otaVariantFor(const CrossPointSettings::ContentProfile profile) {
  switch (profile) {
    case CrossPointSettings::ContentProfile::Global:
      return "global";
    case CrossPointSettings::ContentProfile::China:
      return "cn";
  }
  return "global";
}

inline const char* host() { return hostFor(SETTINGS.contentProfile); }
inline const char* otaVariant() { return otaVariantFor(SETTINGS.contentProfile); }

static_assert(hostFor(CrossPointSettings::ContentProfile::China) == CHINA_HOST);
static_assert(hostFor(CrossPointSettings::ContentProfile::Global) == GLOBAL_HOST);

}  // namespace CrossMuxEndpoints
