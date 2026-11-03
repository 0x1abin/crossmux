#pragma once
#include <OpdsTransport.h>

/**
 * Firmware implementation of the SDK OpdsClient's transport: streaming GET and
 * form POST over HttpDownloader (esp_http_client + wolfSSL), and URL joining
 * through UrlUtils. One instance per configured server keeps its credential
 * origin separate from arbitrary feed, publication, search and cover links.
 */
class OpdsHttpTransport final : public freeink::opds::OpdsTransport {
  std::string serverOrigin;

 public:
  void setServerUrl(const std::string& url);
  freeink::opds::HttpAuth authFor(const std::string& url, freeink::opds::HttpAuth auth) const;

  freeink::opds::HttpResult get(const std::string& url, const freeink::opds::HttpAuth& auth, const char* accept,
                                const char* acceptLanguage, bool captureErrorBody,
                                const freeink::opds::OpdsDataSink& onChunk) override;

  bool postForm(const std::string& url, const std::string& body, std::string& outResponse, int& outStatus) override;

  std::string resolveUrl(const std::string& base, const std::string& ref) override;
};
