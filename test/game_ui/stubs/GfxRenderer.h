#pragma once

#include <string>
#include <vector>

class GfxRenderer {
 public:
  struct TextCall {
    int font;
    std::string text;
    bool black;
  };

  mutable std::vector<TextCall> textCalls;
  mutable int fillCount = 0;
  mutable int rectCount = 0;

  void fillRect(int, int, int, int, bool) const { ++fillCount; }
  void drawRect(int, int, int, int, int, bool) const { ++rectCount; }
  int getTextHeight(int) const { return 12; }
  int getTextWidth(int, const char* text) const { return static_cast<int>(std::string(text).size()) * 6; }
  void drawText(int font, int, int, const char* text, bool black = true) const {
    textCalls.push_back({font, text, black});
  }
};
