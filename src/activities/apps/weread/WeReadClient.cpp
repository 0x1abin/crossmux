#include "WeReadClient.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "WeReadKeyStore.h"
#include "network/HttpDownloader.h"

namespace {

constexpr const char* kGatewayUrl = "https://i.weread.qq.com/api/agent/gateway";
constexpr const char* kSkillVersion = "1.0.4";  // Must match weread-skills/SKILL.md `version:`

int g_lastErrCode = 0;
std::string g_lastUpgradeMessage;

}  // namespace

WeReadClient::Err WeReadClient::post(const char* apiName, JsonDocument& body, JsonDocument& outResp, int httpTimeoutMs,
                                     const JsonDocument* filter) {
  g_lastErrCode = 0;
  g_lastUpgradeMessage.clear();

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("WEREAD", "post(%s): no wifi", apiName);
    return Err::NoWifi;
  }

  const std::string apiKey = WeReadKeyStore::load();
  if (apiKey.empty()) {
    LOG_ERR("WEREAD", "post(%s): no api key", apiName);
    return Err::NoApiKey;
  }

  // Inject required fields. body remains JsonDocument so caller-passed nested
  // arrays/objects survive the injection.
  body["api_name"] = apiName;
  body["skill_version"] = kSkillVersion;

  std::string payload;
  serializeJson(body, payload);

  // POST through HttpDownloader → esp_http_client. TLS is verified against
  // the bundled CA roots (i.weread.qq.com is a public Tencent endpoint with a
  // commercial chain in the Mozilla bundle); the prior setInsecure() path is
  // gone along with the Arduino HTTPClient stack. Body is streamed straight
  // into ArduinoJson; esp_http_client_read transparently strips chunked
  // transfer-encoding so no second-layer framing is needed.
  bool parseFailed = false;
  const bool transportOk = HttpDownloader::postJson(
      kGatewayUrl, payload, apiKey,
      [&](Stream& bodyStream) {
        const bool useFilter = filter != nullptr && filter->size() > 0;
        const DeserializationError parseErr =
            useFilter ? deserializeJson(outResp, bodyStream, DeserializationOption::Filter(*filter))
                      : deserializeJson(outResp, bodyStream);
        if (parseErr) {
          LOG_ERR("WEREAD", "POST %s parse error: %s (filter=%d)", apiName, parseErr.c_str(), useFilter ? 1 : 0);
          parseFailed = true;
          return false;
        }
        return true;
      },
      httpTimeoutMs);

  if (!transportOk) {
    return parseFailed ? Err::Parse : Err::Http;
  }

  // Skill upgrade gate — per SKILL.md §通用规则 1, must halt on this field.
  JsonVariantConst upgrade = outResp["upgrade_info"];
  if (!upgrade.isNull()) {
    g_lastUpgradeMessage = upgrade["message"] | "Skill upgrade required";
    LOG_ERR("WEREAD", "POST %s upgrade_info: %s", apiName, g_lastUpgradeMessage.c_str());
    return Err::Upgrade;
  }

  const int errcode = outResp["errcode"] | 0;
  if (errcode != 0) {
    g_lastErrCode = errcode;
    const char* errmsg = outResp["errmsg"] | outResp["errMsg"] | "";
    LOG_ERR("WEREAD", "POST %s errcode=%d errmsg=%s", apiName, errcode, errmsg);
    return Err::Server;
  }

  LOG_DBG("WEREAD", "POST %s OK", apiName);
  return Err::Ok;
}

int WeReadClient::lastErrCode() { return g_lastErrCode; }

const std::string& WeReadClient::lastUpgradeMessage() { return g_lastUpgradeMessage; }

const char* WeReadClient::errorName(Err err) {
  switch (err) {
    case Err::Ok:
      return "Ok";
    case Err::NoWifi:
      return "NoWifi";
    case Err::NoApiKey:
      return "NoApiKey";
    case Err::Http:
      return "Http";
    case Err::Parse:
      return "Parse";
    case Err::Server:
      return "Server";
    case Err::Upgrade:
      return "Upgrade";
    case Err::NoCache:
      return "NoCache";
  }
  return "Unknown";
}
