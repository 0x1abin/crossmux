#include "PluginHttp.h"

#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "network/ProtectedPaths.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
constexpr size_t MAX_TOKEN_FILE_SIZE = 2 * 1024;

// Resolves the caller's reused TLS session (or `tmp`) into a configured,
// connected client, or nullptr if begin() or the connection failed.
freeink::SecureHttpClient* openClient(freeink::SecureHttpClient* session, freeink::SecureHttpClient& tmp,
                                      const std::string& url, const pluginhttp::Headers& headers,
                                      const std::function<bool()>& shouldAbort = nullptr) {
  freeink::SecureHttpClient& http = session ? *session : tmp;
  http.setUserAgent("CrossPoint");
  // Same trust posture as every other SecureNet consumer: no CA bundle ships
  // with the transport, so verification is skipped; traffic stays encrypted.
  http.setInsecure();
  http.setTimeout(30000);
  if (!http.begin(pluginhttp::escapeUrl(url))) return nullptr;
  // Handshake first: the client's copy of a large header (a session cookie can
  // be several KB) would otherwise take heap the handshake needs.
  if (!http.connect(shouldAbort)) return nullptr;
  for (const auto& h : headers) http.addHeader(h.first, h.second);
  return &http;
}

// A chunked body's Content-Length, if any, is not its size.
bool hasKnownLength(const freeink::SecureHttpClient& http) {
  return http.hasContentLength() && http.getHeader("transfer-encoding").empty();
}
}  // namespace

namespace pluginhttp {

// True when a dotted-path segment addresses an array index.
static bool segIsIndex(const std::string& seg) { return !seg.empty() && isdigit(static_cast<unsigned char>(seg[0])); }

void splitPath(const std::string& dotted, std::vector<std::string>& out) {
  out.clear();
  out.reserve(static_cast<size_t>(std::count(dotted.begin(), dotted.end(), '.')) + 1);
  size_t start = 0;
  while (start <= dotted.size()) {
    const size_t dot = dotted.find('.', start);
    if (dot == std::string::npos) {
      if (start < dotted.size()) out.push_back(dotted.substr(start));
      break;
    }
    if (dot > start) out.push_back(dotted.substr(start, dot - start));
    start = dot + 1;
  }
}

JsonVariantConst resolvePath(JsonVariantConst node, const std::string& dotted) {
  if (dotted.empty()) return node;
  std::vector<std::string> segs;
  splitPath(dotted, segs);
  for (const auto& seg : segs) {
    if (node.isNull()) break;
    if (segIsIndex(seg)) {
      node = node[atoi(seg.c_str())];
    } else {
      node = node[seg.c_str()];
    }
  }
  return node;
}

std::string variantToString(JsonVariantConst v) {
  if (v.isNull()) return "";
  if (v.is<const char*>()) return v.as<const char*>();
  char buf[24];
  if (v.is<long long>() || v.is<int>()) {
    snprintf(buf, sizeof(buf), "%lld", v.as<long long>());
    return buf;
  }
  return "";
}

void substituteAll(std::string& s, const char* key, const std::string& value) {
  const size_t keyLen = strlen(key);
  size_t pos = 0;
  while ((pos = s.find(key, pos)) != std::string::npos) {
    s.replace(pos, keyLen, value);
    pos += value.size();
  }
}

void readRequest(JsonVariantConst node, const char* defaultMethod, RequestSpec& out) {
  out.url = node["url"] | "";
  out.method = node["method"] | defaultMethod;
  out.body = node["body"] | "";
  readHeaders(node["headers"], out.headers);
}

void readHeaders(JsonVariantConst node, Headers& out) {
  out.clear();
  const JsonObjectConst headers = node.as<JsonObjectConst>();
  out.reserve(headers.size());
  for (JsonPairConst kv : headers) {
    out.emplace_back(kv.key().c_str(), kv.value().as<const char*>() ? kv.value().as<const char*>() : "");
  }
}

std::string urlEncodeQuery(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  for (const unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string escapeUrl(const std::string& url) {
  const auto kept = [](const unsigned char c) { return c > 0x20 && c < 0x7F; };
  const size_t escaped =
      std::count_if(url.begin(), url.end(), [&](const char c) { return !kept(static_cast<unsigned char>(c)); });
  std::string out;
  out.reserve(url.size() + 2 * escaped);
  for (const unsigned char c : url) {
    if (kept(c)) {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string inPluginDir(const std::string& pluginDir, const char* path) {
  if (!path || !path[0] || path[0] == '/') return path ? path : "";
  return pluginDir + "/" + path;
}

// A manifest may not point the firmware at a credential store.
static bool allowedFile(const std::string& file) {
  if (protectedpaths::isPluginPath(file)) return true;
  LOG_ERR("PHTP", "Manifest file not allowed: %s", file.c_str());
  return false;
}

static bool tokenSiblingPaths(const std::string& file, std::string& temp, std::string& backup) {
  if (file.size() > SIZE_MAX / 2 - 5) return false;
  const size_t bytes = file.size() + 5;
  const auto heap = HalMemory::getDefaultHeap();
  // Two variable-length cold-path names outlive the I/O; admit both before reserving.
  if (!memory::hasAllocationHeadroom(heap.freeBytes, heap.largestBlockBytes, bytes * 2, bytes, 8192, 0)) {
    LOG_ERR("PHTP", "Low heap for token transaction paths");
    return false;
  }
  temp.reserve(bytes - 1);
  backup.reserve(bytes - 1);
  temp.assign(file).append(".tmp");
  backup.assign(file).append(".bak");
  return true;
}

static bool readTokenRecord(const std::string& file, const std::string& path, std::string* out = nullptr) {
  std::string raw;
  if (!Storage.readFileToString("PHTP", file, MAX_TOKEN_FILE_SIZE, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;
  const auto token = resolvePath(doc.as<JsonVariantConst>(), path);
  if (!token.is<const char*>() && !token.is<int>() && !token.is<long long>()) return false;
  if (out) *out = variantToString(token);
  return true;
}

static bool recoverTokenRecord(const std::string& file, const std::string& path, const std::string& temp,
                               const std::string& backup) {
  if (Storage.exists(file.c_str())) return readTokenRecord(file, path);
  for (const auto* candidate : {&backup, &temp}) {
    if (!Storage.exists(candidate->c_str())) continue;
    if (!readTokenRecord(*candidate, path) || !Storage.rename(candidate->c_str(), file.c_str())) {
      LOG_ERR("PHTP", "Cannot recover token record");
      return false;
    }
    return true;
  }
  return true;  // A new token file has no prior record to recover.
}

bool loadTokenFromFile(const std::string& file, const std::string& path, std::string& out) {
  out.clear();
  if (file.empty()) return true;  // token-less plugin
  if (!allowedFile(file)) return false;
  HalStorage::StorageLock lock;
  if (!Storage.exists(file.c_str())) {
    std::string temp, backup;
    if (!tokenSiblingPaths(file, temp, backup) || !recoverTokenRecord(file, path, temp, backup)) return false;
  }
  return readTokenRecord(file, path, &out) && !out.empty();
}

bool saveTokenToFile(const std::string& file, const std::string& path, const std::string& value) {
  if (file.empty() || !allowedFile(file)) return false;
  HalStorage::StorageLock lock;
  std::string temp, backup;
  if (!tokenSiblingPaths(file, temp, backup) || !recoverTokenRecord(file, path, temp, backup)) return false;
  // Reserved siblings with unknown content cannot safely be discarded.
  for (const auto* sibling : {&temp, &backup}) {
    if (Storage.exists(sibling->c_str()) && (!readTokenRecord(*sibling, path) || !Storage.remove(sibling->c_str()))) {
      LOG_ERR("PHTP", "Cannot clear previous token transaction");
      return false;
    }
  }
  JsonDocument doc;
  std::vector<std::string> segs;
  splitPath(path, segs);
  if (segs.empty() || std::any_of(segs.begin(), segs.end(), segIsIndex)) return false;
  JsonVariant node = doc.to<JsonObject>();
  for (size_t i = 0; i + 1 < segs.size(); i++) node = node[segs[i]].to<JsonObject>();
  node[segs.back()] = value;
  const size_t expected = measureJson(doc);
  if (doc.overflowed() || expected > MAX_TOKEN_FILE_SIZE) {
    LOG_ERR("PHTP", "Invalid or oversized token JSON");
    return false;
  }
  bool complete;
  {
    HalFile output;
    if (!Storage.openFileForWrite("PHTP", temp, output)) return false;
    complete = serializeJson(doc, output) == expected;
    const bool closed = output.close();  // Check the flush before replacing an existing token.
    complete = complete && closed;
  }
  if (!complete) {
    Storage.remove(temp.c_str());
    LOG_ERR("PHTP", "Failed to stage complete token JSON");
    return false;
  }
  const bool hadPrevious = Storage.exists(file.c_str());
  if (hadPrevious && !Storage.rename(file.c_str(), backup.c_str())) {
    Storage.remove(temp.c_str());
    LOG_ERR("PHTP", "Cannot back up token file");
    return false;
  }
  if (!Storage.rename(temp.c_str(), file.c_str())) {
    if (hadPrevious && !Storage.rename(backup.c_str(), file.c_str()))
      LOG_ERR("PHTP", "Token backup retained for later recovery");
    LOG_ERR("PHTP", "Cannot install staged token");
    return false;
  }
  if (hadPrevious && !Storage.remove(backup.c_str())) LOG_ERR("PHTP", "Token saved; backup cleanup deferred");
  return true;
}

void loadConfigFile(const std::string& file, Headers& out) {
  out.clear();
  if (file.empty()) return;
  if (!allowedFile(file)) return;
  std::string raw;
  if (!Storage.readFileToString("PHTP", file, MAX_TOKEN_FILE_SIZE, raw)) return;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return;
  const JsonObjectConst fields = doc.as<JsonObjectConst>();
  out.reserve(fields.size());
  for (JsonPairConst kv : fields) {
    out.emplace_back(kv.key().c_str(), variantToString(kv.value()));
  }
}

int request(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
            const std::string& body, const Headers& headers, String& out, const size_t maxResponse,
            Headers* responseHeaders, const std::function<bool()>& shouldAbort) {
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient tmp;
  freeink::SecureHttpClient* httpPtr = openClient(session, tmp, url, headers, shouldAbort);
  if (!httpPtr) return -1;
  freeink::SecureHttpClient& http = *httpPtr;

  out.remove(0);
  size_t reserved = 0;
  bool overflow = false;
  const int status = http.sendRequest(
      method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        if (len > maxResponse - out.length()) {
          overflow = true;
          return false;
        }
        const size_t needed = out.length() + len;
        if (needed > reserved) {
          // Reserve the whole body once when its length is known; otherwise grow
          // geometrically instead of reallocating for every network chunk.
          const bool knownLength = hasKnownLength(http);
          size_t want = knownLength ? std::max(needed, http.getContentLength())
                                    : std::min(maxResponse, std::max(needed, reserved * 2));
          if (want > maxResponse) {
            LOG_ERR("PHTP", "Response of %u bytes is over the %u byte cap", (unsigned)want, (unsigned)maxResponse);
            overflow = true;
            return false;
          }
          reserved = want;
        }
        if (!out.reserve(reserved) || !out.concat(reinterpret_cast<const char*>(data), len)) {
          overflow = true;
          return false;
        }
        return true;
      },
      shouldAbort);
  if (responseHeaders) *responseHeaders = http.getHeaders();
  // Error statuses still return their body: OAuth device-code polling carries
  // its state ("authorization_pending") in 4xx response bodies.
  if (overflow || status < 0 || !http.responseComplete()) {
    LOG_ERR("PHTP", "API request failed: status=%d overflow=%d complete=%d %s", status, overflow,
            http.responseComplete(), url.c_str());
    return -1;
  }
  return status;
}

int requestStream(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                  const std::string& body, const Headers& headers, const StreamStart& onStart, const StreamData& onData,
                  bool& complete, const std::function<bool()>& shouldAbort) {
  complete = false;
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient tmp;
  freeink::SecureHttpClient* httpPtr = openClient(session, tmp, url, headers, shouldAbort);
  if (!httpPtr) return -1;
  freeink::SecureHttpClient& http = *httpPtr;

  bool started = false;
  bool stopped = false;
  const auto start = [&] {
    const long length =
        hasKnownLength(http) ? static_cast<long>(std::min<size_t>(http.getContentLength(), LONG_MAX)) : -1;
    started = onStart(http.getStatus(), http.getHeaders(), length);
    return started;
  };
  const int status = http.sendRequest(
      method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        if ((!started && !start()) || !onData(data, len)) {
          stopped = true;
          return false;
        }
        return true;
      },
      shouldAbort);
  complete = status >= 0 && !stopped && http.responseComplete();
  // An empty body never calls onData, so start here.
  if (complete && !started && !start()) complete = false;
  if (!complete) {
    LOG_ERR("PHTP", "Streamed request failed: status=%d started=%d stopped=%d %s", status, started, stopped,
            url.c_str());
  }
  return started ? status : -1;
}

int requestToFile(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                  const std::string& body, const Headers& headers, const char* destPath, const size_t maxResponse) {
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient tmp;
  freeink::SecureHttpClient* httpPtr = openClient(session, tmp, url, headers);
  if (!httpPtr) return -1;
  freeink::SecureHttpClient& http = *httpPtr;

  int status = -1;
  bool writeOk = true;
  size_t written = 0;
  {
    HalFile file;
    if (!Storage.openFileForWrite("PHTP", destPath, file)) return -1;
    status = http.sendRequest(method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
                              [&](const uint8_t* data, size_t len) {
                                if (written + len > maxResponse) {
                                  writeOk = false;
                                  return false;
                                }
                                if (file.write(data, len) != len) {
                                  writeOk = false;
                                  return false;
                                }
                                written += len;
                                return true;
                              });
    file.flush();
    // file closed at scope exit, before any Storage.remove of destPath
  }
  if (!writeOk || status < 0 || !http.responseComplete()) {
    LOG_ERR("PHTP", "API request (to file) failed: status=%d writeOk=%d complete=%d %s", status, writeOk,
            http.responseComplete(), url.c_str());
    Storage.remove(destPath);
    return -1;
  }
  return status;
}

bool mintPasswordToken(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                       const std::string& body, const Headers& headers, const std::string& tokenPath,
                       std::string& outToken) {
  String response;
  constexpr size_t MAX_AUTH_RESPONSE = 8 * 1024;
  const int status = request(session, url, method, body, headers, response, MAX_AUTH_RESPONSE);
  if (status < 200 || status >= 300) return false;
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) return false;
  outToken = variantToString(resolvePath(doc.as<JsonVariantConst>(), tokenPath));
  return !outToken.empty();
}

}  // namespace pluginhttp
