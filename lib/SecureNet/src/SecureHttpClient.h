#pragma once

#include <Arduino.h>
#include <esp_http_client.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

/**
 * Lightweight HTTP(S) client used by the network layer (KOReader sync, OPDS
 * downloads, OTA). The API mirrors the original SecureNet `SecureHttpClient`
 * that lived in the (now absent) freeink-sdk submodule, so the callers in
 * HttpDownloader.cpp and KOReaderSyncClient.cpp compile unchanged.
 *
 * This port is backed by the ESP-IDF `esp_http_client` (the same transport used
 * by the non-wolfSSL path elsewhere in the project) rather than a hand-rolled
 * wolfSSL stack. It is a drop-in replacement for the original API surface.
 */

namespace freeink {

class SecureHttpClient {
 public:
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  using CancelCallback = std::function<bool()>;

  SecureHttpClient();
  ~SecureHttpClient();

  SecureHttpClient(const SecureHttpClient&) = delete;
  SecureHttpClient& operator=(const SecureHttpClient&) = delete;

  // Disable TLS host/chain verification (self-signed / custom CA targets).
  void setInsecure();

  // Per-socket-op timeout in milliseconds.
  void setTimeout(uint32_t milliseconds);

  // Replace the built-in User-Agent header (must be called after begin()).
  void setUserAgent(const char* userAgent);

  // Add a request header (must be called after begin()).
  void addHeader(const char* name, const std::string& value);

  // Open a connection to `url`. Returns false for invalid URLs.
  bool begin(const std::string& url);

  // Release the connection and any buffered response.
  void end();

  // Blocking GET; returns the HTTP status code (<=0 on transport error).
  int GET();

  // Streaming GET. `onData` may return false to abort the body; `isCancelled`
  // (optional) aborts the body when it returns true.
  int GET(DataCallback onData, CancelCallback isCancelled = nullptr);

  // Send a request with a body (POST/PUT/...). Returns the status code.
  int sendRequest(const char* method, const std::string& body);

  // Buffered response body (for non-streaming requests).
  String getString();

  int getStatus() const { return status_; }
  bool hasContentLength() const { return hasContentLength_; }
  size_t getContentLength() const { return contentLength_; }
  bool aborted() const { return aborted_; }
  bool callbackAborted() const { return callbackAborted_; }
  bool responseComplete() const { return complete_; }

  // Read a single response header value (valid after the request completes).
  std::string getHeader(const char* name);

  // Resolve `relative` against `base` per RFC 3986; false on failure.
  static bool resolveUrl(const std::string& base, const std::string& relative, std::string& out);

 private:
  int perform(const char* method, const std::string& body, DataCallback onData, CancelCallback isCancelled);

  // esp_http_client event callback: captures response headers (e.g. Location)
  // that must be readable after GET() returns. Registered via user_data = this.
  static esp_err_t onHttpEvent(esp_http_client_event_t* evt);

  void* handle_ = nullptr;  // esp_http_client_handle_t
  bool insecure_ = false;
  uint32_t timeoutMs_ = 0;
  std::string response_;
  std::vector<std::pair<std::string, std::string>> lastResponseHeaders_;
  int status_ = 0;
  size_t contentLength_ = 0;
  bool hasContentLength_ = false;
  bool complete_ = false;
  bool aborted_ = false;
  bool callbackAborted_ = false;
};

}  // namespace freeink