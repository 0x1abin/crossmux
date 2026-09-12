#pragma once

#include <optional>
#include <string>
#include <utility>

struct WifiCredential {
  std::string ssid;
  std::string password;
};

class WifiCredentialStore {
 public:
  static WifiCredentialStore& getInstance() {
    static WifiCredentialStore store;
    return store;
  }

  size_t getCredentialCount() const { return credential_ ? 1 : 0; }
  bool loadFromFile() { return true; }
  std::string getLastConnectedSsid() const { return credential_ ? credential_->ssid : ""; }
  std::optional<WifiCredential> findCredential(const std::string& ssid) const {
    return credential_ && credential_->ssid == ssid ? credential_ : std::nullopt;
  }
  void setCredential(std::string ssid, std::string password) {
    credential_ = WifiCredential{std::move(ssid), std::move(password)};
  }
  void reset() { credential_.reset(); }

 private:
  std::optional<WifiCredential> credential_;
};

#define WIFI_STORE WifiCredentialStore::getInstance()
