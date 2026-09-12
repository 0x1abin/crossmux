#pragma once

#include <cstdint>

class WiFiClient;

class PubSubClient {
 public:
  explicit PubSubClient(WiFiClient&) {}

  PubSubClient& setServer(const char*, uint16_t) { return *this; }
  PubSubClient& setCallback(void (*)(char*, uint8_t*, unsigned int)) { return *this; }
  PubSubClient& setSocketTimeout(uint16_t) { return *this; }
  PubSubClient& setKeepAlive(uint16_t) { return *this; }
  bool connect(const char*, const char*, uint8_t, bool, const char*) { return false; }
  bool subscribe(const char*) { return false; }
  bool publish(const char*, const char*, bool) { return false; }
  bool connected() const { return false; }
  void disconnect() {}
  bool loop() { return false; }
  int state() const { return -1; }
};
