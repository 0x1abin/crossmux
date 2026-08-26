#include <gtest/gtest.h>

#include <string>

class GfxRenderer {};

#include "AirPageConnection.h"
#include "WifiCredentialStore.h"
#include "esp_wifi.h"

namespace airpage {

const std::string& deviceId() {
  static const std::string id = "test-device";
  return id;
}

}  // namespace airpage

namespace {

void resetFakes() {
  WiFi.reset();
  WIFI_STORE.reset();
  espWifiDeinitCalls = 0;
}

TEST(AirPageConnectionTest, ManualModeDoesNotStartWifiAssociation) {
  resetFakes();
  WIFI_STORE.setCredential("saved", "secret");
  GfxRenderer renderer;
  airpage::AirPageConnection connection(renderer);

  EXPECT_EQ(connection.begin(false), airpage::AirPageConnection::Event::None);
  EXPECT_EQ(connection.state(), airpage::AirPageConnection::State::Off);
  EXPECT_EQ(WiFi.beginCalls, 0);
}

TEST(AirPageConnectionTest, LiveModeSilentlyUsesSavedCredential) {
  resetFakes();
  WIFI_STORE.setCredential("saved", "secret");
  GfxRenderer renderer;
  airpage::AirPageConnection connection(renderer);

  EXPECT_EQ(connection.begin(true), airpage::AirPageConnection::Event::StateChanged);
  EXPECT_EQ(connection.state(), airpage::AirPageConnection::State::WifiConnecting);
  EXPECT_EQ(WiFi.beginCalls, 1);
  EXPECT_EQ(WiFi.lastSsid, "saved");
  EXPECT_EQ(WiFi.lastPassword, "secret");
}

TEST(AirPageConnectionTest, LiveModeRequestsWifiWithoutSavedCredential) {
  resetFakes();
  GfxRenderer renderer;
  airpage::AirPageConnection connection(renderer);

  EXPECT_EQ(connection.begin(true), airpage::AirPageConnection::Event::WifiRequired);
  EXPECT_EQ(connection.state(), airpage::AirPageConnection::State::Off);
  EXPECT_EQ(WiFi.beginCalls, 0);
}

TEST(AirPageConnectionTest, DisablingLiveModeStopsOwnedWifiAssociation) {
  resetFakes();
  WIFI_STORE.setCredential("saved", "secret");
  GfxRenderer renderer;
  airpage::AirPageConnection connection(renderer);
  ASSERT_EQ(connection.begin(true), airpage::AirPageConnection::Event::StateChanged);
  WiFi.statusValue = WL_CONNECTED;

  EXPECT_EQ(connection.setRealtime(false), airpage::AirPageConnection::Event::StateChanged);
  EXPECT_EQ(connection.state(), airpage::AirPageConnection::State::Off);
  EXPECT_EQ(WiFi.modeValue, WIFI_OFF);
  EXPECT_EQ(espWifiDeinitCalls, 1);
}

TEST(AirPageConnectionTest, EnablingLiveModeUsesSameSilentAssociationPath) {
  resetFakes();
  WIFI_STORE.setCredential("saved", "secret");
  GfxRenderer renderer;
  airpage::AirPageConnection connection(renderer);
  ASSERT_EQ(connection.begin(false), airpage::AirPageConnection::Event::None);

  EXPECT_EQ(connection.setRealtime(true), airpage::AirPageConnection::Event::StateChanged);
  EXPECT_EQ(connection.state(), airpage::AirPageConnection::State::WifiConnecting);
  EXPECT_EQ(WiFi.beginCalls, 1);
}

}  // namespace
