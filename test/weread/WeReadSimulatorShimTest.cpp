#include <MD5Builder.h>
#include <esp_http_client.h>
#include <esp_sntp.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "WeReadHttpClient.h"

namespace {

struct HeaderCapture {
  std::vector<std::pair<std::string, std::string>> fields;
};

esp_err_t captureHeader(esp_http_client_event_t* event) {
  if (!event || event->event_id != HTTP_EVENT_ON_HEADER || !event->user_data) return ESP_FAIL;
  auto* capture = static_cast<HeaderCapture*>(event->user_data);
  capture->fields.emplace_back(event->header_key, event->header_value);
  return ESP_OK;
}

}  // namespace

TEST(WeReadSimulatorShim, ComputesRealMd5) {
  MD5Builder md5;
  md5.begin();
  md5.add(reinterpret_cast<const uint8_t*>("a"), 1);
  md5.add(reinterpret_cast<const uint8_t*>("bc"), 2);
  md5.calculate();
  EXPECT_STREQ(md5.toString().c_str(), "900150983cd24fb0d6963f7d28e17f72");
}

TEST(WeReadHttpClient, ExtractsOnlyBoundedHttpsHosts) {
  WeReadHttpClient::HttpsUrlView view;
  ASSERT_TRUE(WeReadHttpClient::parseHttpsUrl("https://Res.WeRead.QQ.Com/path/image.jpg", view));
  EXPECT_EQ(std::string(view.host, view.hostLength), "Res.WeRead.QQ.Com");
  EXPECT_STREQ(view.path, "/path/image.jpg");

  char host[128];
  ASSERT_TRUE(WeReadHttpClient::extractHttpsHost("https://Res.WeRead.QQ.Com/path/image.jpg", host, sizeof(host)));
  EXPECT_STREQ(host, "res.weread.qq.com");
  EXPECT_FALSE(WeReadHttpClient::extractHttpsHost("http://res.weread.qq.com/image.jpg", host, sizeof(host)));
  EXPECT_FALSE(WeReadHttpClient::extractHttpsHost("https://user@res.weread.qq.com/image.jpg", host, sizeof(host)));
  EXPECT_FALSE(WeReadHttpClient::extractHttpsHost("https://res..weread.qq.com/image.jpg", host, sizeof(host)));
  EXPECT_FALSE(
      WeReadHttpClient::extractHttpsHost("https://res.weread.qq.com/image.jpg\nX-Test: bad", host, sizeof(host)));
  char shortHost[8];
  EXPECT_FALSE(WeReadHttpClient::extractHttpsHost("https://res.weread.qq.com/image.jpg", shortHost, sizeof(shortHost)));
}

TEST(WeReadHttpClient, SimulatorNetworkIsReady) { EXPECT_TRUE(WeReadHttpClient::networkReady()); }

TEST(WeReadSimulatorShim, SupportsCooperativeSntpCalls) {
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();
  EXPECT_FALSE(esp_sntp_enabled());
  EXPECT_EQ(sntp_get_sync_status(), SNTP_SYNC_STATUS_RESET);
  esp_sntp_stop();
}

TEST(WeReadSimulatorShim, DeliversRedirectAndFinalResponseHeaders) {
  HeaderCapture capture;
  esp_http_client_config_t config = {};
  config.url = "https://example.invalid";
  config.event_handler = captureHeader;
  config.user_data = &capture;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  ASSERT_NE(client, nullptr);

  char status[] = "HTTP/1.1 302 Found\r\n";
  char redirectCookie[] = "Set-Cookie: wr_rt=redirect; Path=/\r\n";
  char finalCookie[] = "Set-Cookie:\twr_skey=final; HttpOnly\r\n";
  EXPECT_EQ(esp_http_client_shim_detail::headerCb(status, 1, std::strlen(status), client), std::strlen(status));
  EXPECT_EQ(esp_http_client_shim_detail::headerCb(redirectCookie, 1, std::strlen(redirectCookie), client),
            std::strlen(redirectCookie));
  EXPECT_EQ(esp_http_client_shim_detail::headerCb(finalCookie, 1, std::strlen(finalCookie), client),
            std::strlen(finalCookie));

  ASSERT_EQ(capture.fields.size(), 2u);
  EXPECT_EQ(capture.fields[0], std::make_pair(std::string("Set-Cookie"), std::string("wr_rt=redirect; Path=/")));
  EXPECT_EQ(capture.fields[1], std::make_pair(std::string("Set-Cookie"), std::string("wr_skey=final; HttpOnly")));
  EXPECT_EQ(esp_http_client_cleanup(client), ESP_OK);
}

TEST(WeReadSimulatorShim, ReadsSpooledBodyAcrossSmallChunksWithVerifiedTls) {
  static_assert(esp_http_client_shim_detail::kVerifyTls);
  esp_http_client_config_t config = {};
  config.url = "https://example.invalid";
  esp_http_client_handle_t client = esp_http_client_init(&config);
  ASSERT_NE(client, nullptr);
  ASSERT_EQ(esp_http_client_open(client, 0), ESP_OK);

  constexpr char payload[] = "0123456789abcdef";
  ASSERT_EQ(esp_http_client_shim_detail::writeCb(const_cast<char*>(payload), 1, sizeof(payload) - 1, client),
            sizeof(payload) - 1);
  ASSERT_EQ(std::fflush(client->resp_file), 0);
  ASSERT_EQ(std::fseek(client->resp_file, 0, SEEK_SET), 0);

  std::string actual;
  char chunk[3];
  while (true) {
    const int got = esp_http_client_read(client, chunk, sizeof(chunk));
    ASSERT_GE(got, 0);
    if (got == 0) break;
    actual.append(chunk, static_cast<size_t>(got));
  }
  EXPECT_EQ(actual, payload);
  EXPECT_EQ(esp_http_client_cleanup(client), ESP_OK);
}

TEST(WeReadSimulatorShim, ReusesHandleAcrossChaptersAndResetsPerRequestState) {
  esp_http_client_config_t config = {};
  config.url = "https://example.invalid/chapter/0";
  esp_http_client_handle_t client = esp_http_client_init(&config);
  ASSERT_NE(client, nullptr);
  CURL* const curl = client->curl;

  for (int chapter = 1; chapter <= 3; ++chapter) {
    const std::string url = "https://example.invalid/chapter/" + std::to_string(chapter);
    const std::string cookie = "chapter=" + std::to_string(chapter);
    ASSERT_EQ(esp_http_client_set_url(client, url.c_str()), ESP_OK);
    ASSERT_EQ(esp_http_client_set_header(client, chapter == 1 ? "Cookie" : "cookie", cookie.c_str()), ESP_OK);
    ASSERT_EQ(esp_http_client_set_method(client, HTTP_METHOD_POST), ESP_OK);
    ASSERT_EQ(esp_http_client_set_timeout_ms(client, 1234), ESP_OK);
    ASSERT_EQ(esp_http_client_open(client, 3), ESP_OK);

    EXPECT_EQ(client->curl, curl);
    EXPECT_EQ(client->url, url);
    ASSERT_EQ(client->headers.size(), 1U);
    EXPECT_EQ(client->headers[0].second, cookie);
    EXPECT_TRUE(client->req_body.empty());
    EXPECT_FALSE(client->performed);
    EXPECT_EQ(client->status, 0);
    EXPECT_EQ(client->content_length, -1);
    EXPECT_FALSE(client->complete);
    EXPECT_TRUE(esp_http_client_is_persistent_connection(client));

    ASSERT_EQ(esp_http_client_write(client, "abc", 3), 3);
    client->performed = true;
    client->status = 200;
    client->content_length = 3;
    client->complete = true;
  }
  EXPECT_EQ(esp_http_client_cleanup(client), ESP_OK);
}

TEST(WeReadSimulatorShim, RecreatesHandleAfterServerConnectionClose) {
  esp_http_client_config_t config = {};
  config.url = "https://example.invalid";
  esp_http_client_handle_t client = esp_http_client_init(&config);
  ASSERT_NE(client, nullptr);

  char closeHeader[] = "Connection: close\r\n";
  ASSERT_EQ(esp_http_client_shim_detail::headerCb(closeHeader, 1, std::strlen(closeHeader), client),
            std::strlen(closeHeader));
  EXPECT_FALSE(esp_http_client_is_persistent_connection(client));
  EXPECT_EQ(esp_http_client_cleanup(client), ESP_OK);

  client = esp_http_client_init(&config);
  ASSERT_NE(client, nullptr);
  EXPECT_TRUE(esp_http_client_is_persistent_connection(client));
  EXPECT_EQ(esp_http_client_cleanup(client), ESP_OK);
  EXPECT_EQ(esp_http_client_cleanup(nullptr), ESP_FAIL);
}
