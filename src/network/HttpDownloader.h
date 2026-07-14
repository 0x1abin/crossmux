#pragma once
#include <HalStorage.h>

#include <functional>
#include <string>

// Forward-declared: fetchUrl() takes a Stream& by reference. On-device this name
// arrives transitively via the SdFat/Arduino chain in <HalStorage.h>; declaring
// it here keeps the header self-sufficient (the host build's SdFat shim doesn't
// pull Arduino's Stream in transitively).
class Stream;

/**
 * HTTP client utility for fetching content and downloading files. GETs use
 * the configured SecureNet transport; JSON POST keeps the verified
 * esp_http_client CA-bundle path used by the Chinese WeRead integration.
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
  };

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "");

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "");

  // application/json POST with Bearer auth. Response body is exposed to
  // onResponse as a pull-style Stream so the caller (e.g. ArduinoJson) can
  // deserialize it without buffering. HTTPS is verified via the ESP CA bundle;
  // plain HTTP remains available for local servers. Returns false on
  // any transport / status failure, or if onResponse returns false.
  static bool postJson(const std::string& url, const std::string& payload, const std::string& bearerToken,
                       const std::function<bool(Stream&)>& onResponse, int timeoutMs = 60000);
};
