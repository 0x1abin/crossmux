#include "SecureHttpClient.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <cstring>
#include <vector>

namespace freeink {
namespace {
constexpr int HTTP_RX_BUF = 4096;
constexpr int HTTP_TX_BUF = 512;
constexpr int READ_CHUNK = 1024;

// Normalize a path: collapse duplicate slashes and resolve "." / ".." segments.
std::string normalizePath(const std::string& path) {
  std::vector<std::string> segs;
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    std::string seg = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (seg == "." || seg.empty()) {
      // skip
    } else if (seg == "..") {
      if (!segs.empty()) segs.pop_back();
    } else {
      segs.push_back(seg);
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  std::string out;
  for (const auto& s : segs) out += "/" + s;
  if (out.empty()) out = "/";
  return out;
}
}  // namespace

SecureHttpClient::SecureHttpClient() = default;

SecureHttpClient::~SecureHttpClient() { end(); }

void SecureHttpClient::setInsecure() { insecure_ = true; }

void SecureHttpClient::setTimeout(uint32_t milliseconds) { timeoutMs_ = milliseconds; }

void SecureHttpClient::setUserAgent(const char* userAgent) {
  if (handle_) esp_http_client_set_header(static_cast<esp_http_client_handle_t>(handle_), "User-Agent", userAgent);
}

void SecureHttpClient::addHeader(const char* name, const std::string& value) {
  if (handle_) esp_http_client_set_header(static_cast<esp_http_client_handle_t>(handle_), name, value.c_str());
}

bool SecureHttpClient::begin(const std::string& url) {
  end();
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = static_cast<int>(timeoutMs_);
  config.buffer_size = HTTP_RX_BUF;
  config.buffer_size_tx = HTTP_TX_BUF;
  config.keep_alive_enable = true;
  // esp-tls refuses a client that has neither a CA store nor the
  // CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY build option ("No server verification
  // option set"), so always attach the CRT bundle to make the TLS setup pass.
  // In insecure mode we additionally skip the common-name check; the chain is
  // still validated, which is fine for the public HTTPS hosts we fetch.
  config.crt_bundle_attach = esp_crt_bundle_attach;
  if (insecure_) {
    config.skip_cert_common_name_check = true;
  }
  // Without an event handler esp_http_client never stores response headers, so
  // esp_http_client_get_header("Location") returns empty and redirects fail.
  // Capture them here and serve them from getHeader().
  config.event_handler = &SecureHttpClient::onHttpEvent;
  config.user_data = this;
  handle_ = esp_http_client_init(&config);
  return handle_ != nullptr;
}

esp_err_t SecureHttpClient::onHttpEvent(esp_http_client_event_t* evt) {
  auto* self = static_cast<SecureHttpClient*>(evt->user_data);
  if (!self) return ESP_OK;
  if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value) {
    std::string key = evt->header_key;
    for (auto& c : key) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    self->lastResponseHeaders_.emplace_back(std::move(key), std::string(evt->header_value));
  }
  return ESP_OK;
}

void SecureHttpClient::end() {
  if (handle_) {
    esp_http_client_cleanup(static_cast<esp_http_client_handle_t>(handle_));
    handle_ = nullptr;
  }
  response_.clear();
  status_ = 0;
  contentLength_ = 0;
  hasContentLength_ = false;
  complete_ = false;
  aborted_ = false;
  callbackAborted_ = false;
}

int SecureHttpClient::GET() { return perform("GET", "", nullptr, nullptr); }

int SecureHttpClient::GET(DataCallback onData, CancelCallback isCancelled) {
  return perform("GET", "", std::move(onData), std::move(isCancelled));
}

int SecureHttpClient::sendRequest(const char* method, const std::string& body) {
  return perform(method, body, nullptr, nullptr);
}

int SecureHttpClient::perform(const char* methodStr, const std::string& body, DataCallback onData,
                              CancelCallback isCancelled) {
  if (!handle_) return -1;

  esp_http_client_method_t method;
  if (std::strcmp(methodStr, "POST") == 0) {
    method = HTTP_METHOD_POST;
  } else if (std::strcmp(methodStr, "PUT") == 0) {
    method = HTTP_METHOD_PUT;
  } else if (std::strcmp(methodStr, "DELETE") == 0) {
    method = HTTP_METHOD_DELETE;
  } else {
    method = HTTP_METHOD_GET;
  }
  esp_http_client_set_method(static_cast<esp_http_client_handle_t>(handle_), method);

  response_.clear();
  lastResponseHeaders_.clear();
  status_ = 0;
  aborted_ = false;
  callbackAborted_ = false;

  const esp_err_t err = esp_http_client_open(static_cast<esp_http_client_handle_t>(handle_), static_cast<int>(body.size()));
  if (err != ESP_OK) return -1;

  if (!body.empty()) {
    const int written = esp_http_client_write(static_cast<esp_http_client_handle_t>(handle_), body.data(),
                                              static_cast<int>(body.size()));
    if (written < 0) return -1;
  }

  const int64_t length = esp_http_client_fetch_headers(static_cast<esp_http_client_handle_t>(handle_));
  const int status = esp_http_client_get_status_code(static_cast<esp_http_client_handle_t>(handle_));
  status_ = status;
  hasContentLength_ = length > 0;
  contentLength_ = length > 0 ? static_cast<size_t>(length) : 0;

  char buf[READ_CHUNK];
  while (true) {
    if (isCancelled && isCancelled()) {
      aborted_ = true;
      break;
    }
    const int r = esp_http_client_read(static_cast<esp_http_client_handle_t>(handle_), buf, READ_CHUNK);
    if (r < 0) break;
    if (r == 0) break;
    if (onData) {
      if (!onData(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(r))) {
        callbackAborted_ = true;
        break;
      }
    } else {
      response_.append(buf, static_cast<size_t>(r));
    }
  }

  complete_ = esp_http_client_is_complete_data_received(static_cast<esp_http_client_handle_t>(handle_));
  return status;
}

String SecureHttpClient::getString() { return String(response_.c_str(), static_cast<unsigned int>(response_.size())); }

std::string SecureHttpClient::getHeader(const char* name) {
  // Prefer the response headers captured by the event handler (the open/read
  // path doesn't populate esp_http_client's own header store).
  const std::string key = [&]() {
    std::string k = name;
    for (auto& c : k) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return k;
  }();
  for (const auto& pair : lastResponseHeaders_) {
    if (pair.first == key) return pair.second;
  }
  if (!handle_) return "";
  char* value = nullptr;
  if (esp_http_client_get_header(static_cast<esp_http_client_handle_t>(handle_), name, &value) == ESP_OK && value) {
    return std::string(value);
  }
  return "";
}

bool SecureHttpClient::resolveUrl(const std::string& base, const std::string& relative, std::string& out) {
  // Absolute URL.
  if (relative.find("://") != std::string::npos) {
    out = relative;
    return true;
  }
  // Scheme-relative (//host/path).
  if (relative.rfind("//", 0) == 0) {
    const size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return false;
    out = base.substr(0, schemeEnd + 3) + relative.substr(2);
    return true;
  }

  const size_t schemeEnd = base.find("://");
  if (schemeEnd == std::string::npos) return false;
  const std::string scheme = base.substr(0, schemeEnd + 3);

  const size_t pathStart = base.find('/', schemeEnd + 3);
  std::string authority;
  std::string basePath;
  if (pathStart == std::string::npos) {
    authority = base.substr(schemeEnd + 3);
    basePath = "/";
  } else {
    authority = base.substr(schemeEnd + 3, pathStart - (schemeEnd + 3));
    basePath = base.substr(pathStart);
  }

  std::string merged;
  if (relative.empty()) {
    merged = basePath;
  } else if (relative[0] == '/') {
    merged = relative;
  } else {
    const size_t slash = basePath.rfind('/');
    const std::string dir = (slash == std::string::npos) ? "/" : basePath.substr(0, slash + 1);
    merged = dir + relative;
  }

  out = scheme + authority + normalizePath(merged);
  return true;
}

}  // namespace freeink