#pragma once
#include <HalStorage.h>

#include <functional>
#include <string>

#if defined(ENABLE_CHINESE_VERSION) && !defined(__EMSCRIPTEN__)
#include <esp_http_client.h>
#endif

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
#if defined(ENABLE_CHINESE_VERSION) && !defined(__EMSCRIPTEN__)
  using HeaderCallback = std::function<void(const char* name, const char* value)>;

  class VerifiedSession {
   public:
    VerifiedSession() = default;
    ~VerifiedSession();
    VerifiedSession(const VerifiedSession&) = delete;
    VerifiedSession& operator=(const VerifiedSession&) = delete;

    bool reusable() const { return client_ != nullptr; }
    void reset();

   private:
    friend class HttpDownloader;
    esp_http_client_handle_t client_ = nullptr;
  };

  struct Header {
    const char* name;
    const char* value;
  };

  struct RequestOptions {
    const char* method = "GET";
    const uint8_t* body = nullptr;
    size_t bodySize = 0;
    const Header* headers = nullptr;
    size_t headerCount = 0;
    int timeoutMs = 60000;
    uint8_t* readBuffer = nullptr;
    size_t readBufferSize = 0;
  };
#endif

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

#if defined(ENABLE_CHINESE_VERSION) && !defined(__EMSCRIPTEN__)
  // Verified esp_http_client request with caller-supplied headers and streaming
  // response callbacks. Unlike fetchUrl(), non-2xx status codes are returned to
  // the caller through `status` after their body has been streamed.
  static DownloadError requestVerified(const char* url, const RequestOptions& options, const DataCallback& onData,
                                       const HeaderCallback& onHeader, int& status);
  static DownloadError requestVerified(VerifiedSession& session, const char* url, const RequestOptions& options,
                                       const DataCallback& onData, const HeaderCallback& onHeader, int& status);
#endif
};
