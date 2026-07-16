#pragma once

#include <vector>

#include "WeReadFetchActivity.h"
#include "WeReadModels.h"

class WeReadShelfActivity final : public WeReadFetchActivity {
 public:
  WeReadShelfActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~WeReadShelfActivity() override = default;

 protected:
  const char* apiName() const override { return "/shelf/sync"; }
  void buildRequest(JsonDocument& body) override;
  void buildResponseFilter(JsonDocument& filter) override;
  void parseResponse(JsonDocument& resp) override;
  int itemCount() const override { return static_cast<int>(books_.size()); }
  const char* headerTitle() const override;
  void renderContent(Rect contentRect) override;
  void onConfirm(int index) override;
  bool tryLoadFromCache() override;

 private:
  std::vector<WeReadModels::BookCard> books_;
  int ebookCount_ = 0;
  int albumCount_ = 0;
  int mpCount_ = 0;
};
