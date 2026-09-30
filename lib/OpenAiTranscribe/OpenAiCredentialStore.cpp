#include "OpenAiCredentialStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

namespace {
constexpr size_t MASK_VISIBLE_SUFFIX = 4;
constexpr char MASK_PREFIX[] = "********";

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
}  // namespace

void OpenAiCredentialStore::toJson(JsonDocument& doc) const { doc["key_obf"] = obfuscation::obfuscateToBase64(apiKey); }

bool OpenAiCredentialStore::fromJson(JsonVariantConst doc) {
  bool ok = false;
  bool tooLong = false;
  std::string key = obfuscation::deobfuscateFromBase64(doc["key_obf"] | "", MAX_KEY_LENGTH, &ok, &tooLong);
  if (!ok || tooLong) {
    LOG_ERR("OAI", "Stored API key is unreadable; ignoring it");
    key.clear();
  }
  apiKey = std::move(key);
  return true;
}

void OpenAiCredentialStore::setApiKey(const std::string& key) {
  size_t begin = 0;
  size_t end = key.size();
  while (begin < end && isSpace(key[begin])) ++begin;
  while (end > begin && isSpace(key[end - 1])) --end;
  if (end - begin > MAX_KEY_LENGTH) {
    LOG_ERR("OAI", "API key too long (%u chars); not saved", static_cast<unsigned>(end - begin));
    return;
  }
  apiKey.assign(key, begin, end - begin);
  LOG_INF("OAI", "API key %s", apiKey.empty() ? "cleared" : "set");
}

std::string OpenAiCredentialStore::maskedApiKey() const {
  if (apiKey.empty()) return {};
  std::string masked = MASK_PREFIX;
  if (apiKey.size() > MASK_VISIBLE_SUFFIX * 2)
    masked.append(apiKey, apiKey.size() - MASK_VISIBLE_SUFFIX, std::string::npos);
  return masked;
}

void OpenAiCredentialStore::applyWebValue(const std::string& value) {
  if (!value.empty() && value == maskedApiKey()) return;
  setApiKey(value);
  saveToFile();
}
