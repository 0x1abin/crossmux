#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

/**
 * OpenAI API key for the Voice Notes app, stored on the SD card in
 * /.crosspoint/openai.json. The key is XOR-obfuscated with the device MAC and
 * base64-encoded (same scheme as the KOReader and Wi-Fi stores): not
 * encryption, but keeps it from being read casually and ties it to this chip.
 */
class OpenAiCredentialStore : public PersistableStore<OpenAiCredentialStore> {
 private:
  std::string apiKey;

  OpenAiCredentialStore() = default;
  ~OpenAiCredentialStore() = default;

  friend class PersistableStore<OpenAiCredentialStore>;

 public:
  // OpenAI keys are ~50-170 chars; anything longer is a paste error.
  static constexpr size_t MAX_KEY_LENGTH = 256;

  static const char* getFilePath() { return "/.crosspoint/openai.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::string& getApiKey() const { return apiKey; }
  bool hasApiKey() const { return !apiKey.empty(); }
  // Trims surrounding whitespace; an empty value clears the key.
  void setApiKey(const std::string& key);

  // Display-only form for the web settings page: never returns the key itself.
  std::string maskedApiKey() const;
  // Web settings setter: ignores the unchanged mask, clears on empty.
  void applyWebValue(const std::string& value);
};

#define OPENAI_STORE OpenAiCredentialStore::getInstance()
