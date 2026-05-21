#pragma once
// libcurl-backed HTTPClient/WiFiClient shim. WeRead and other firmware code paths
// that exchange HTTPS with the cloud now actually hit the host's network. The
// public method shapes mirror the Arduino-ESP32 HTTPClient API just enough to
// satisfy the call sites we keep in the simulator build.

#include <Stream.h>
#include <WString.h>
#include <curl/curl.h>

#include <cstdint>
#include <string>

enum HTTPC_FOLLOW_REDIRECTS_T {
  HTTPC_DISABLE_FOLLOW_REDIRECTS = 0,
  HTTPC_STRICT_FOLLOW_REDIRECTS,
  HTTPC_FORCE_FOLLOW_REDIRECTS,
};

#ifndef HTTP_CODE_OK
#define HTTP_CODE_OK 200
#endif

class WiFiClient {
 public:
  virtual ~WiFiClient() = default;
  bool connect(const char*, uint16_t) { return false; }
  void stop() {}
  bool connected() { return true; }  // libcurl owns the socket; treat as up.
  int available() { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t*, size_t) { return 0; }
};

// Modern Arduino-ESP32 uses NetworkClient as the base type. Simulator collapses
// the hierarchy onto WiFiClient since libcurl already drives the transport.
using NetworkClient = WiFiClient;

class HTTPClient {
 public:
  HTTPClient() = default;
  ~HTTPClient() { end(); }

  HTTPClient(const HTTPClient&) = delete;
  HTTPClient& operator=(const HTTPClient&) = delete;

  bool begin(const String& url) {
    end();
    curl_ = curl_easy_init();
    if (!curl_) return false;
    url_ = std::string(url.c_str());
    return true;
  }

  bool begin(WiFiClient& /*client*/, const String& url) { return begin(url); }

  void end() {
    if (curl_) {
      curl_easy_cleanup(curl_);
      curl_ = nullptr;
    }
    if (headers_) {
      curl_slist_free_all(headers_);
      headers_ = nullptr;
    }
    responseBody_.clear();
    bodyStream_.reset(responseBody_);
  }

  void setTimeout(uint16_t ms) { timeoutMs_ = ms; }
  void setFollowRedirects(int policy) { followRedirects_ = (policy != HTTPC_DISABLE_FOLLOW_REDIRECTS); }
  void setAuthorization(const char*, const char*) {}

  void addHeader(const String& name, const String& value) {
    std::string line(name.c_str());
    line.append(": ");
    line.append(value.c_str());
    headers_ = curl_slist_append(headers_, line.c_str());
  }

  int GET() { return perform_(false, nullptr, 0); }

  int POST(const String& body) { return POST(reinterpret_cast<const uint8_t*>(body.c_str()), body.length()); }
  int POST(const uint8_t* body, size_t size) { return perform_(true, body, size); }

  String getString() { return String(responseBody_.c_str()); }
  Stream& getStream() {
    bodyStream_.reset(responseBody_);
    return bodyStream_;
  }
  Stream* getStreamPtr() { return &getStream(); }
  int getSize() { return static_cast<int>(responseBody_.size()); }

  // Drive the caller's sink with the already-downloaded body (libcurl delivered
  // it whole during perform_). Mirrors HTTPClient::writeToStream — returns bytes
  // written, or stops early if the sink refuses (e.g. a cancel flag). Used by
  // HttpDownloader to stream the response into an FsFile.
  int writeToStream(Stream* stream) {
    if (!stream) return -1;
    const size_t total = responseBody_.size();
    size_t off = 0;
    while (off < total) {
      size_t chunk = total - off;
      if (chunk > 4096) chunk = 4096;
      const size_t w = stream->write(reinterpret_cast<const uint8_t*>(responseBody_.data() + off), chunk);
      if (w == 0) break;  // sink refused (cancelled / write error)
      off += w;
    }
    return static_cast<int>(off);
  }

 private:
  // libcurl delivers the whole body before perform_ returns, so the
  // "stream" is just a memory cursor over responseBody_.
  class BodyStream : public Stream {
   public:
    void reset(const std::string& body) {
      body_ = &body;
      pos_ = 0;
    }
    int available() override { return body_ ? static_cast<int>(body_->size() - pos_) : 0; }
    int read() override {
      if (!body_ || pos_ >= body_->size()) return -1;
      return static_cast<unsigned char>((*body_)[pos_++]);
    }
    int peek() override {
      if (!body_ || pos_ >= body_->size()) return -1;
      return static_cast<unsigned char>((*body_)[pos_]);
    }
    size_t write(uint8_t) override { return 0; }
    void flush() override {}

   private:
    const std::string* body_ = nullptr;
    size_t pos_ = 0;
  };

  static size_t writeCb_(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* self = static_cast<HTTPClient*>(userdata);
    const size_t n = size * nmemb;
    self->responseBody_.append(ptr, n);
    return n;
  }

  int perform_(bool isPost, const uint8_t* body, size_t size) {
    if (!curl_) return -1;
    responseBody_.clear();
    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
    if (isPost) {
      curl_easy_setopt(curl_, CURLOPT_POST, 1L);
      curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body);
      curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(size));
    } else {
      curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    }
    if (headers_) curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, followRedirects_ ? 1L : 0L);
    if (timeoutMs_) curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs_));
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, &writeCb_);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, this);
    // Mirror NetworkClientSecure::setInsecure(): tolerate self-signed / out-of-date
    // certs in dev. Tighten later if a real cert chain is wired up.
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);

    const CURLcode rc = curl_easy_perform(curl_);
    bodyStream_.reset(responseBody_);
    if (rc != CURLE_OK) return -static_cast<int>(rc);

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    return static_cast<int>(httpCode);
  }

  CURL* curl_ = nullptr;
  curl_slist* headers_ = nullptr;
  std::string url_;
  std::string responseBody_;
  BodyStream bodyStream_;
  uint16_t timeoutMs_ = 0;
  bool followRedirects_ = false;
};
