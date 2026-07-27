#include "WeReadHttpClient.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include <cctype>
#include <cstring>

#if defined(FREEINK_NET_WOLFSSL) && !defined(CROSSPOINT_EMULATED)
#include <strings.h>

#include <limits>
#else
#include <esp_crt_bundle.h>
#endif

namespace {

bool containsNewline(const char* value) { return value && (strchr(value, '\r') || strchr(value, '\n')); }

bool copyHttpsUrlParts(const char* url, char* host, const size_t hostSize, const char*& path) {
  WeReadHttpClient::HttpsUrlView view;
  if (!host || hostSize < 2 || !WeReadHttpClient::parseHttpsUrl(url, view) || view.hostLength >= hostSize) return false;
  for (size_t i = 0; i < view.hostLength; ++i) {
    host[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(view.host[i])));
  }
  host[view.hostLength] = '\0';
  path = view.path;
  return true;
}

#if defined(FREEINK_NET_WOLFSSL) && !defined(CROSSPOINT_EMULATED)
constexpr uint16_t HTTPS_PORT = 443;

enum class TransferResult : uint8_t {
  Ok,
  Aborted,
  Error,
};

enum class BodyFraming : uint8_t {
  None,
  ContentLength,
  Chunked,
  CloseDelimited,
};

void cleanupClient(freeink::SecureClient& client) {
  const bool hadConnection = client.connected();
  client.stop();
  if (!hadConnection) return;
  LOG_DBG("WR", "wolfSSL TLS closed: free=%u largest=%u stack=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

bool hasHeaderToken(const char* value, const char* token) {
  if (!value || !token || !*token) return false;
  const size_t tokenLength = strlen(token);
  while (*value) {
    while (*value == ' ' || *value == '\t' || *value == ',') ++value;
    const char* end = strchr(value, ',');
    if (!end) end = value + strlen(value);
    while (end > value && (end[-1] == ' ' || end[-1] == '\t')) --end;
    if (static_cast<size_t>(end - value) == tokenLength && strncasecmp(value, token, tokenLength) == 0) return true;
    value = *end ? end + 1 : end;
  }
  return false;
}

bool parseDecimalSize(const char* value, size_t& result) {
  if (!value || !*value) return false;
  size_t parsed = 0;
  for (const char* cursor = value; *cursor; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    const size_t digit = static_cast<size_t>(*cursor - '0');
    if (parsed > (std::numeric_limits<size_t>::max() - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  result = parsed;
  return true;
}

bool parseHexSize(const char* value, size_t& result) {
  if (!value || !*value) return false;
  size_t parsed = 0;
  bool sawDigit = false;
  for (const char* cursor = value; *cursor && *cursor != ';'; ++cursor) {
    unsigned digit = 0;
    if (*cursor >= '0' && *cursor <= '9') {
      digit = static_cast<unsigned>(*cursor - '0');
    } else if (*cursor >= 'a' && *cursor <= 'f') {
      digit = static_cast<unsigned>(*cursor - 'a' + 10);
    } else if (*cursor >= 'A' && *cursor <= 'F') {
      digit = static_cast<unsigned>(*cursor - 'A' + 10);
    } else {
      return false;
    }
    sawDigit = true;
    if (parsed > (std::numeric_limits<size_t>::max() - digit) / 16) return false;
    parsed = parsed * 16 + digit;
  }
  result = parsed;
  return sawDigit;
}

bool writeAll(freeink::SecureClient& client, const uint8_t* data, const size_t length) {
  size_t written = 0;
  while (written < length) {
    const size_t count = client.write(data + written, length - written);
    if (count == 0) return false;
    written += count;
  }
  return true;
}

bool writeText(freeink::SecureClient& client, const char* text) {
  return text && writeAll(client, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool readLine(freeink::SecureClient& client, uint8_t* buffer, const size_t capacity, const int timeoutMs,
              size_t& length) {
  if (!buffer || capacity < 2) return false;
  length = 0;
  const unsigned long deadline = millis() + static_cast<unsigned long>(timeoutMs);
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    const int value = client.read();
    if (value >= 0) {
      if (value == '\n') {
        if (length > 0 && buffer[length - 1] == '\r') --length;
        buffer[length] = '\0';
        return true;
      }
      if (length + 1 >= capacity) {
        LOG_ERR("HTTP", "wolfSSL line too long: capacity=%u", static_cast<unsigned>(capacity));
        return false;
      }
      buffer[length++] = static_cast<uint8_t>(value);
      continue;
    }
    if (!client.connected() && client.available() == 0) return false;
    delay(1);
  }
  return false;
}

TransferResult readFixedBody(freeink::SecureClient& client, uint8_t* buffer, const size_t bufferSize,
                             const size_t length, const int timeoutMs, const WeReadHttpClient::DataCallback& onData) {
  size_t remaining = length;
  unsigned long deadline = millis() + static_cast<unsigned long>(timeoutMs);
  while (remaining > 0) {
    const size_t wanted = remaining < bufferSize ? remaining : bufferSize;
    const int count = client.read(buffer, wanted);
    if (count > 0) {
      const size_t received = static_cast<size_t>(count);
      if (onData && !onData(buffer, received)) return TransferResult::Aborted;
      remaining -= received;
      deadline = millis() + static_cast<unsigned long>(timeoutMs);
      continue;
    }
    if (count < 0 || (!client.connected() && client.available() == 0) ||
        static_cast<int32_t>(millis() - deadline) >= 0) {
      return TransferResult::Error;
    }
    delay(1);
  }
  return TransferResult::Ok;
}

TransferResult readCloseDelimitedBody(freeink::SecureClient& client, uint8_t* buffer, const size_t bufferSize,
                                      const int timeoutMs, const WeReadHttpClient::DataCallback& onData) {
  unsigned long deadline = millis() + static_cast<unsigned long>(timeoutMs);
  while (true) {
    const int count = client.read(buffer, bufferSize);
    if (count > 0) {
      if (onData && !onData(buffer, static_cast<size_t>(count))) return TransferResult::Aborted;
      deadline = millis() + static_cast<unsigned long>(timeoutMs);
      continue;
    }
    if (!client.connected() && client.available() == 0) return TransferResult::Ok;
    if (count < 0 || static_cast<int32_t>(millis() - deadline) >= 0) return TransferResult::Error;
    delay(1);
  }
}

TransferResult readChunkedBody(freeink::SecureClient& client, uint8_t* buffer, const size_t bufferSize,
                               const int timeoutMs, const WeReadHttpClient::DataCallback& onData,
                               const WeReadHttpClient::HeaderCallback& onHeader) {
  while (true) {
    size_t lineLength = 0;
    if (!readLine(client, buffer, bufferSize, timeoutMs, lineLength)) return TransferResult::Error;
    size_t chunkSize = 0;
    if (!parseHexSize(reinterpret_cast<const char*>(buffer), chunkSize)) return TransferResult::Error;
    if (chunkSize == 0) {
      while (true) {
        if (!readLine(client, buffer, bufferSize, timeoutMs, lineLength)) return TransferResult::Error;
        if (lineLength == 0) return TransferResult::Ok;
        char* line = reinterpret_cast<char*>(buffer);
        char* colon = strchr(line, ':');
        if (!colon || colon == line) return TransferResult::Error;
        *colon = '\0';
        char* value = colon + 1;
        while (*value == ' ' || *value == '\t') ++value;
        if (onHeader) onHeader(line, value);
      }
    }

    const TransferResult result = readFixedBody(client, buffer, bufferSize, chunkSize, timeoutMs, onData);
    if (result != TransferResult::Ok) return result;
    if (!readLine(client, buffer, bufferSize, timeoutMs, lineLength) || lineLength != 0) {
      return TransferResult::Error;
    }
  }
}

WeReadHttpClient::Result runRequest(const char* url, const WeReadHttpClient::RequestOptions& options,
                                    const WeReadHttpClient::DataCallback& onData,
                                    const WeReadHttpClient::HeaderCallback& onHeader, int& status,
                                    freeink::SecureClient& client, char* sessionHost, const size_t sessionHostSize,
                                    uint32_t& newConnections, uint32_t& reusedRequests) {
  status = -1;
  char host[128];
  const char* path = nullptr;
  if (!url || !options.method || (strcmp(options.method, "GET") != 0 && strcmp(options.method, "POST") != 0) ||
      (options.bodySize > 0 && !options.body) || (options.headerCount > 0 && !options.headers) ||
      options.timeoutMs <= 0 || !options.readBuffer || options.readBufferSize < 2 ||
      !copyHttpsUrlParts(url, host, sizeof(host), path) || !sessionHost || sessionHostSize < sizeof(host)) {
    return WeReadHttpClient::Result::NetworkError;
  }

  for (size_t i = 0; i < options.headerCount; ++i) {
    const auto& header = options.headers[i];
    if (!header.name || !header.value || containsNewline(header.name) || containsNewline(header.value)) {
      return WeReadHttpClient::Result::NetworkError;
    }
  }

  if (client.connected() && strcmp(sessionHost, host) != 0) {
    LOG_INF("WR", "TLS host switch: %s -> %s", sessionHost, host);
    cleanupClient(client);
  }
  const bool reused = client.connected();
  LOG_DBG("WR", "wolfSSL TLS %s: host=%s free=%u largest=%u stack=%u", reused ? "reused" : "new", host,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

  client.setInsecure();
  client.setTimeout(static_cast<unsigned long>(options.timeoutMs));
  if (!reused && !client.connect(host, HTTPS_PORT)) {
    LOG_ERR("HTTP", "wolfSSL request connect failed");
    cleanupClient(client);
    return WeReadHttpClient::Result::NetworkError;
  }
  if (reused) {
    ++reusedRequests;
  } else {
    ++newConnections;
  }
  memcpy(sessionHost, host, strlen(host) + 1);

  bool hasUserAgent = false;
  char contentLength[32];
  const int contentLengthSize =
      snprintf(contentLength, sizeof(contentLength), "Content-Length: %u\r\n", static_cast<unsigned>(options.bodySize));
  bool requestWritten = writeText(client, options.method) && writeText(client, " ") && writeText(client, path) &&
                        writeText(client, " HTTP/1.1\r\nHost: ") && writeText(client, host) &&
                        writeText(client, "\r\nConnection: keep-alive\r\n");
  for (size_t i = 0; i < options.headerCount; ++i) {
    const auto& header = options.headers[i];
    hasUserAgent |= strcasecmp(header.name, "User-Agent") == 0;
    requestWritten = requestWritten && writeText(client, header.name) && writeText(client, ": ") &&
                     writeText(client, header.value) && writeText(client, "\r\n");
  }
  if (!hasUserAgent) {
    requestWritten = requestWritten && writeText(client, "User-Agent: CrossPoint-ESP32-" CROSSPOINT_VERSION "\r\n");
  }
  if (strcmp(options.method, "POST") == 0) {
    requestWritten = requestWritten && contentLengthSize > 0 &&
                     static_cast<size_t>(contentLengthSize) < sizeof(contentLength) && writeText(client, contentLength);
  }
  requestWritten = requestWritten && writeText(client, "\r\n") &&
                   (options.bodySize == 0 || writeAll(client, options.body, options.bodySize));
  if (!requestWritten) {
    LOG_ERR("HTTP", "wolfSSL request write failed");
    cleanupClient(client);
    return WeReadHttpClient::Result::NetworkError;
  }

  size_t lineLength = 0;
  if (!readLine(client, options.readBuffer, options.readBufferSize, options.timeoutMs, lineLength)) {
    LOG_ERR("HTTP", "wolfSSL status read failed");
    cleanupClient(client);
    return WeReadHttpClient::Result::NetworkError;
  }
  const char* statusLine = reinterpret_cast<const char*>(options.readBuffer);
  if (lineLength < 12 || strncmp(statusLine, "HTTP/1.", 7) != 0 || (statusLine[7] != '0' && statusLine[7] != '1') ||
      statusLine[8] != ' ' || statusLine[9] < '0' || statusLine[9] > '9' || statusLine[10] < '0' ||
      statusLine[10] > '9' || statusLine[11] < '0' || statusLine[11] > '9') {
    LOG_ERR("HTTP", "wolfSSL invalid status line");
    cleanupClient(client);
    return WeReadHttpClient::Result::NetworkError;
  }
  status = (statusLine[9] - '0') * 100 + (statusLine[10] - '0') * 10 + statusLine[11] - '0';
  bool keepAlive = statusLine[7] != '0';
  bool hasContentLength = false;
  bool hasTransferEncoding = false;
  bool chunked = false;
  bool peerRequestedClose = false;
  size_t responseLength = 0;

  while (true) {
    if (!readLine(client, options.readBuffer, options.readBufferSize, options.timeoutMs, lineLength)) {
      LOG_ERR("HTTP", "wolfSSL header read failed");
      cleanupClient(client);
      return WeReadHttpClient::Result::NetworkError;
    }
    if (lineLength == 0) break;
    char* line = reinterpret_cast<char*>(options.readBuffer);
    char* colon = strchr(line, ':');
    if (!colon || colon == line) {
      LOG_ERR("HTTP", "wolfSSL invalid response header");
      cleanupClient(client);
      return WeReadHttpClient::Result::NetworkError;
    }
    *colon = '\0';
    char* value = colon + 1;
    while (*value == ' ' || *value == '\t') ++value;
    char* valueEnd = value + strlen(value);
    while (valueEnd > value && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) *--valueEnd = '\0';
    if (onHeader) onHeader(line, value);

    if (strcasecmp(line, "Content-Length") == 0) {
      size_t parsedLength = 0;
      if (!parseDecimalSize(value, parsedLength) || (hasContentLength && parsedLength != responseLength)) {
        cleanupClient(client);
        return WeReadHttpClient::Result::NetworkError;
      }
      responseLength = parsedLength;
      hasContentLength = true;
    } else if (strcasecmp(line, "Transfer-Encoding") == 0) {
      hasTransferEncoding = true;
      chunked |= hasHeaderToken(value, "chunked");
    } else if (strcasecmp(line, "Connection") == 0) {
      if (hasHeaderToken(value, "close")) {
        peerRequestedClose = true;
        keepAlive = false;
      } else if (!peerRequestedClose && hasHeaderToken(value, "keep-alive")) {
        keepAlive = true;
      }
    }
  }

  BodyFraming framing = BodyFraming::CloseDelimited;
  if ((status >= 100 && status < 200) || status == 204 || status == 304) {
    framing = BodyFraming::None;
  } else if (hasTransferEncoding) {
    if (!chunked) {
      LOG_ERR("HTTP", "wolfSSL unsupported transfer encoding");
      cleanupClient(client);
      return WeReadHttpClient::Result::NetworkError;
    }
    framing = BodyFraming::Chunked;
  } else if (hasContentLength) {
    framing = BodyFraming::ContentLength;
  }

  TransferResult transfer = TransferResult::Ok;
  switch (framing) {
    case BodyFraming::None:
      break;
    case BodyFraming::ContentLength:
      transfer =
          readFixedBody(client, options.readBuffer, options.readBufferSize, responseLength, options.timeoutMs, onData);
      break;
    case BodyFraming::Chunked:
      transfer =
          readChunkedBody(client, options.readBuffer, options.readBufferSize, options.timeoutMs, onData, onHeader);
      break;
    case BodyFraming::CloseDelimited:
      keepAlive = false;
      transfer = readCloseDelimitedBody(client, options.readBuffer, options.readBufferSize, options.timeoutMs, onData);
      break;
  }

  switch (transfer) {
    case TransferResult::Ok:
      break;
    case TransferResult::Aborted:
      cleanupClient(client);
      return WeReadHttpClient::Result::Aborted;
    case TransferResult::Error:
      LOG_ERR("HTTP", "wolfSSL response incomplete");
      cleanupClient(client);
      return WeReadHttpClient::Result::NetworkError;
  }

  const bool persistent = keepAlive && client.connected();
  if (!persistent) {
    LOG_INF("WR", "TLS peer closed: host=%s", host);
    cleanupClient(client);
  }
  LOG_DBG("WR", "wolfSSL TLS response: host=%s persistent=%u free=%u largest=%u stack=%u", host, persistent ? 1U : 0U,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  return WeReadHttpClient::Result::Ok;
}
#else
constexpr int HTTP_RX_BUF = 2048;
constexpr int HTTP_TX_BUF = 1024;

struct RequestEventContext {
  const WeReadHttpClient::HeaderCallback* onHeader = nullptr;
};

void cleanupClient(esp_http_client_handle_t& client) {
  if (!client) return;
  esp_http_client_set_user_data(client, nullptr);
  esp_http_client_cleanup(client);
  client = nullptr;
  LOG_DBG("WR", "TLS closed: free=%u largest=%u stack=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

esp_err_t onRequestEvent(esp_http_client_event_t* event) {
  auto* context = static_cast<RequestEventContext*>(event->user_data);
  if (event->event_id == HTTP_EVENT_ON_HEADER && context && context->onHeader && *context->onHeader &&
      event->header_key && event->header_value) {
    (*context->onHeader)(event->header_key, event->header_value);
  }
  return ESP_OK;
}

WeReadHttpClient::Result runRequest(const char* url, const WeReadHttpClient::RequestOptions& options,
                                    const WeReadHttpClient::DataCallback& onData,
                                    const WeReadHttpClient::HeaderCallback& onHeader, int& status,
                                    esp_http_client_handle_t& client, char* sessionHost, const size_t sessionHostSize,
                                    uint32_t& newConnections, uint32_t& reusedRequests) {
  status = -1;
  char host[128];
  const char* path = nullptr;
  if (!url || !options.method || (strcmp(options.method, "GET") != 0 && strcmp(options.method, "POST") != 0) ||
      (options.bodySize > 0 && !options.body) || (options.headerCount > 0 && !options.headers) ||
      options.timeoutMs <= 0 || !options.readBuffer || options.readBufferSize == 0 ||
      !copyHttpsUrlParts(url, host, sizeof(host), path) || !sessionHost || sessionHostSize < sizeof(host)) {
    return WeReadHttpClient::Result::NetworkError;
  }

  if (client && strcmp(sessionHost, host) != 0) {
    LOG_INF("WR", "TLS host switch: %s -> %s", sessionHost, host);
    cleanupClient(client);
  }
  const bool reused = client != nullptr;
  LOG_DBG("WR", "TLS %s: host=%s free=%u largest=%u stack=%u", reused ? "reused" : "new", host,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  RequestEventContext eventContext{&onHeader};
  const esp_http_client_method_t method = strcmp(options.method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  if (client) {
    if (esp_http_client_set_url(client, url) != ESP_OK || esp_http_client_set_method(client, method) != ESP_OK ||
        esp_http_client_set_timeout_ms(client, options.timeoutMs) != ESP_OK ||
        esp_http_client_set_user_data(client, &eventContext) != ESP_OK) {
      LOG_ERR("HTTP", "verified request reuse setup failed");
      cleanupClient(client);
      return WeReadHttpClient::Result::NetworkError;
    }
  } else {
    esp_http_client_config_t config = {};
    config.url = url;
    config.buffer_size = HTTP_RX_BUF;
    config.buffer_size_tx = HTTP_TX_BUF;
    config.timeout_ms = options.timeoutMs;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.method = method;
    config.keep_alive_enable = false;
    config.event_handler = onRequestEvent;
    config.user_data = &eventContext;

    client = esp_http_client_init(&config);
    if (!client) {
      LOG_ERR("HTTP", "verified request init failed");
      return WeReadHttpClient::Result::NetworkError;
    }
  }
  if (reused) {
    ++reusedRequests;
  } else {
    ++newConnections;
  }
  memcpy(sessionHost, host, strlen(host) + 1);

  if (esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION) != ESP_OK) {
    cleanupClient(client);
    return WeReadHttpClient::Result::NetworkError;
  }
  for (size_t i = 0; i < options.headerCount; ++i) {
    const auto& header = options.headers[i];
    if (header.name && header.value && esp_http_client_set_header(client, header.name, header.value) != ESP_OK) {
      cleanupClient(client);
      return WeReadHttpClient::Result::NetworkError;
    }
  }

  esp_err_t err = esp_http_client_open(client, static_cast<int>(options.bodySize));
  if (err != ESP_OK) {
    LOG_ERR("HTTP", "verified request open failed: %s", esp_err_to_name(err));
    cleanupClient(client);
    return WeReadHttpClient::Result::NetworkError;
  }

  size_t sent = 0;
  while (sent < options.bodySize) {
    const int written = esp_http_client_write(client, reinterpret_cast<const char*>(options.body + sent),
                                              static_cast<int>(options.bodySize - sent));
    if (written <= 0) {
      LOG_ERR("HTTP", "verified request body write failed after %u bytes", static_cast<unsigned>(sent));
      cleanupClient(client);
      return WeReadHttpClient::Result::NetworkError;
    }
    sent += static_cast<size_t>(written);
  }

  if (esp_http_client_fetch_headers(client) < 0) {
    LOG_ERR("HTTP", "verified request header read failed");
    cleanupClient(client);
    return WeReadHttpClient::Result::NetworkError;
  }
  status = esp_http_client_get_status_code(client);

  while (true) {
    const int got = esp_http_client_read(client, reinterpret_cast<char*>(options.readBuffer),
                                         static_cast<int>(options.readBufferSize));
    if (got < 0) {
      LOG_ERR("HTTP", "verified request read failed");
      cleanupClient(client);
      return WeReadHttpClient::Result::NetworkError;
    }
    if (got == 0) break;
    if (onData && !onData(options.readBuffer, static_cast<size_t>(got))) {
      cleanupClient(client);
      return WeReadHttpClient::Result::Aborted;
    }
  }

  const bool complete = esp_http_client_is_complete_data_received(client);
  const bool persistent = complete && esp_http_client_is_persistent_connection(client);
  esp_http_client_set_user_data(client, nullptr);
  if (!persistent) {
    LOG_INF("WR", "TLS peer closed: host=%s", host);
    cleanupClient(client);
  }
  LOG_DBG("WR", "TLS response: host=%s persistent=%u free=%u largest=%u stack=%u", host, persistent ? 1U : 0U,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  return complete ? WeReadHttpClient::Result::Ok : WeReadHttpClient::Result::NetworkError;
}
#endif

}  // namespace

namespace WeReadHttpClient {

bool parseHttpsUrl(const char* url, HttpsUrlView& view) {
  view = {};
  static constexpr char kPrefix[] = "https://";
  if (!url || strncmp(url, kPrefix, sizeof(kPrefix) - 1) != 0 || strchr(url, '\r') || strchr(url, '\n') ||
      strchr(url, '#')) {
    return false;
  }
  view.host = url + sizeof(kPrefix) - 1;
  view.path = strchr(view.host, '/');
  if (!view.path || view.path == view.host || view.host[0] == '.' || view.path[-1] == '.') return false;
  view.hostLength = static_cast<size_t>(view.path - view.host);
  bool previousDot = false;
  for (size_t i = 0; i < view.hostLength; ++i) {
    const unsigned char value = static_cast<unsigned char>(view.host[i]);
    if (!std::isalnum(value) && value != '.' && value != '-') return false;
    if (value == '.' && previousDot) return false;
    previousDot = value == '.';
  }
  return true;
}

bool extractHttpsHost(const char* url, char* host, const size_t hostSize) {
  HttpsUrlView view;
  if (!host || hostSize < 2 || !parseHttpsUrl(url, view) || view.hostLength >= hostSize) return false;
  for (size_t i = 0; i < view.hostLength; ++i) {
    host[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(view.host[i])));
  }
  host[view.hostLength] = '\0';
  return true;
}

bool networkReady() {
#ifdef CROSSPOINT_EMULATED
  return true;
#else
  const wifi_mode_t mode = WiFi.getMode();
  return (mode & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED;
#endif
}

Session::~Session() { reset(); }

bool Session::reusable() {
#if defined(FREEINK_NET_WOLFSSL) && !defined(CROSSPOINT_EMULATED)
  return host_[0] && client_.connected();
#else
  return host_[0] && client_ != nullptr;
#endif
}

void Session::reset() {
  cleanupClient(client_);
  host_[0] = '\0';
}

void Session::clearStats() {
  newConnections_ = 0;
  reusedRequests_ = 0;
}

Result request(const char* url, const RequestOptions& options, const DataCallback& onData,
               const HeaderCallback& onHeader, int& status) {
  if (!networkReady()) {
    status = -1;
    LOG_INF("HTTP", "Request skipped: Wi-Fi not ready");
    return Result::NetworkError;
  }
  Session session;
  return request(session, url, options, onData, onHeader, status);
}

Result request(Session& session, const char* url, const RequestOptions& options, const DataCallback& onData,
               const HeaderCallback& onHeader, int& status) {
  if (!networkReady()) {
    status = -1;
    LOG_INF("HTTP", "Request skipped: Wi-Fi not ready");
    return Result::NetworkError;
  }
  LOG_DBG("HTTP", "%s %s", options.method ? options.method : "?", url ? url : "?");
  return runRequest(url, options, onData, onHeader, status, session.client_, session.host_, sizeof(session.host_),
                    session.newConnections_, session.reusedRequests_);
}

}  // namespace WeReadHttpClient
