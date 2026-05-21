#pragma once
// Host stub for knolleary/PubSubClient. The simulator has no MQTT broker, so
// AirPage's live-push mode never connects (connect()/connected() return false)
// and the face behaves exactly like manual mode. Including NetworkClient.h also
// brings the WiFiClient type into scope for AirPageFace's member declaration
// (the device pulls WiFiClient from <WiFi.h>, which the host shim omits).

#include <NetworkClient.h>

#include <cstdint>

class PubSubClient {
 public:
  PubSubClient() = default;
  template <typename T>
  explicit PubSubClient(T&) {}

  typedef void (*Callback)(char*, uint8_t*, unsigned int);

  PubSubClient& setServer(const char*, uint16_t) { return *this; }
  PubSubClient& setCallback(Callback) { return *this; }
  bool connect(const char*) { return false; }
  bool connect(const char*, const char*, const char*) { return false; }
  bool connected() { return false; }
  bool loop() { return false; }
  bool subscribe(const char*) { return false; }
  bool publish(const char*, const char*) { return false; }
  void disconnect() {}
  int state() { return -1; }
};
