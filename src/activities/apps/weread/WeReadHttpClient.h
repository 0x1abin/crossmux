#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#if defined(FREEINK_NET_WOLFSSL) && !defined(CROSSPOINT_EMULATED)
#include <SecureClient.h>
#else
#include <esp_http_client.h>
#endif

namespace WeReadHttpClient {

using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;
using HeaderCallback = std::function<void(const char* name, const char* value)>;

enum class Result : uint8_t {
  Ok,
  NetworkError,
  Aborted,
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

class Session {
 public:
  Session() = default;
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  bool reusable();
  void reset();

 private:
  friend Result request(Session& session, const char* url, const RequestOptions& options, const DataCallback& onData,
                        const HeaderCallback& onHeader, int& status);
#if defined(FREEINK_NET_WOLFSSL) && !defined(CROSSPOINT_EMULATED)
  freeink::SecureClient client_;
#else
  esp_http_client_handle_t client_ = nullptr;
#endif
};

Result request(const char* url, const RequestOptions& options, const DataCallback& onData,
               const HeaderCallback& onHeader, int& status);
Result request(Session& session, const char* url, const RequestOptions& options, const DataCallback& onData,
               const HeaderCallback& onHeader, int& status);

}  // namespace WeReadHttpClient
