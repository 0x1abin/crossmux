#pragma once

#include <cstdint>
#include <string>

class GfxRenderer;

namespace HeaderDateUtils {

struct DisplayDateInfo {
  uint32_t timestamp = 0;
  bool usedFallback = false;
};

DisplayDateInfo getDisplayDateInfo();
std::string getDisplayDateText();
std::string getSyncDayReminderText();
void drawTopLine(const GfxRenderer& renderer, const std::string& dateText);
void drawHeaderWithDate(const GfxRenderer& renderer, const char* title, const char* subtitle = nullptr);

}  // namespace HeaderDateUtils
