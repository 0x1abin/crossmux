#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <HalSystem.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
// clang-format on

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "CrossMuxEndpoints.h"
#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"

namespace {
constexpr std::string_view nightlyTagPrefix = "nightly-";
constexpr size_t nightlyShaLength = 7;

constexpr bool isSameNightlyBuild(const std::string_view currentVersion, const std::string_view latestTag) {
  if (!latestTag.starts_with(nightlyTagPrefix) || latestTag.size() != nightlyTagPrefix.size() + nightlyShaLength) {
    return false;
  }

  const std::string_view latestSha = latestTag.substr(nightlyTagPrefix.size());
  const bool validSha = std::all_of(latestSha.begin(), latestSha.end(), [](const char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  });
  if (!validSha) return false;

  const size_t separator = currentVersion.rfind('+');
  return separator != std::string_view::npos && currentVersion.substr(separator + 1) == latestSha;
}

static_assert(isSameNightlyBuild("1.5.2-rc+5064d90", "nightly-5064d90"));
static_assert(isSameNightlyBuild("1.5.2-cn-rc+5064d90", "nightly-5064d90"));
static_assert(!isSameNightlyBuild("1.5.2-rc+5064d90", "nightly-1234567"));
static_assert(!isSameNightlyBuild("1.5.2", "nightly-5064d90"));
static_assert(!isSameNightlyBuild("1.5.2-rc+5064d90", "nightly"));

// The locked content profile supplies the OTA host and release asset variant.
// The web proxy re-exposes it as a minimal
// GitHub-release-shaped JSON whose single asset is always named "firmware.bin"
// — that's the literal ReleaseJsonParser matches on.
//
// Going through the web instead of api.github.com directly avoids the
// unauthenticated 60 req/hr/IP rate limit and the unstable mainland-China
// path to api.github.com.
constexpr size_t releaseUrlCapacity = 192;
static_assert(releaseUrlCapacity < 256);
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate(const Channel requestedChannel) {
  channel = requestedChannel;
  char releaseUrl[releaseUrlCapacity];
  const char* channelQuery = channel == Channel::Nightly ? "&channel=nightly" : "";
  const int releaseUrlLength =
      snprintf(releaseUrl, sizeof(releaseUrl), CrossMuxEndpoints::OTA_MANIFEST_FORMAT, CrossMuxEndpoints::host(),
               CrossMuxEndpoints::otaVariant(), channelQuery, HalSystem::getDeviceModel());
  if (releaseUrlLength < 0 || static_cast<size_t>(releaseUrlLength) >= sizeof(releaseUrl)) {
    LOG_ERR("OTA", "Release URL exceeds %zu bytes", sizeof(releaseUrl));
    return INTERNAL_UPDATE_ERROR;
  }
  LOG_DBG("OTA", "Checking %s channel (current: %s)", channel == Channel::Nightly ? "nightly" : "stable",
          CROSSPOINT_VERSION);

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the configured secure GET transport,
  // redirects, and User-Agent (see HttpDownloader).
  ReleaseJsonParser releaseParser(releaseNotes);
  const bool ok = HttpDownloader::fetchUrl(releaseUrl, [&releaseParser](const uint8_t* data, size_t len) {
    releaseParser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (releaseParser.foundUnsupportedChannel()) {
    LOG_INF("OTA", "Selected update channel is not supported by this device");
    return UNSUPPORTED_CHANNEL;
  }

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_ERR("OTA", "No firmware.bin asset found");
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;
  releaseNoteCount = releaseParser.getReleaseNoteCount();

  LOG_DBG("OTA", "Found update: tag=%s size=%zu notes=%zu", latestVersion.c_str(), otaSize, releaseNoteCount);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty()) {
    return false;
  }
  switch (channel) {
    case Channel::Stable:
      break;
    case Channel::Nightly:
      return !isSameNightlyBuild(CROSSPOINT_VERSION, latestVersion);
  }
  if (latestVersion == CROSSPOINT_VERSION) return false;

  int currentMajor, currentMinor, currentPatch;
  int latestMajor, latestMinor, latestPatch;

  const auto currentVersion = CROSSPOINT_VERSION;

  // semantic version check (only match on 3 segments)
  sscanf(latestVersion.c_str(), "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  sscanf(currentVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

  /*
   * Compare major versions.
   * If they differ, return true if latest major version greater than current major version
   * otherwise return false.
   */
  if (latestMajor != currentMajor) return latestMajor > currentMajor;

  /*
   * Compare minor versions.
   * If they differ, return true if latest minor version greater than current minor version
   * otherwise return false.
   */
  if (latestMinor != currentMinor) return latestMinor > currentMinor;

  /*
   * Check patch versions.
   */
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // If we reach here, it means all segments are equal.
  // One final check, if we're on an RC build (contains "-rc"), we should consider the latest version as newer even if
  // the segments are equal, since RC builds are pre-release versions.
  if (strstr(currentVersion, "-rc") != nullptr) {
    return true;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  processedSize = 0;
  int lastReportedPct = -1;
  bool flashOk = true;
  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12)
  // and reject a wrong-MCU image before it overwrites the OTA partition.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  board_tag::Scanner boardScanner;
  bool wrongBoard = false;
  const bool fetchOk = HttpDownloader::fetchUrl(otaUrl, [&](const uint8_t* data, size_t len) {
    if (hdrLen < sizeof(hdr)) {
      const size_t take = std::min(len, sizeof(hdr) - hdrLen);
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          wrongChip = true;
          return false;  // abort the transfer
        }
      }
    }
    boardScanner.feed(data, len);
    if (boardScanner.mismatch()) {
      LOG_ERR("OTA", "wrong board: image=%s device=%.*s", boardScanner.foundName(),
              static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
      wrongBoard = true;
      return false;  // abort before selecting the incomplete image as bootable
    }
    if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
      flashOk = false;
      return false;  // abort the transfer
    }
    processedSize += len;
    // Fire the callback only on whole-percent change. Per-chunk updates wake the
    // render task, whose framebuffer work contends with TLS on the internal arena,
    // and e-ink can't repaint faster than a percent tick anyway.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
      if (pct != lastReportedPct) {
        lastReportedPct = pct;
        onProgress(ctx);
      }
    }
    return true;
  });

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (wrongChip || wrongBoard) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }

  if (!fetchOk || !flashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    esp_ota_abort(otaHandle);
    return flashOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
