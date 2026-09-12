#include <cassert>
#include <cstdio>
#include <initializer_list>

#include "CalculatorFont.h"
#include "builtinFonts/notosans_cjk_12.h"

int main() {
  const EpdFont previousFont(&notosans_cjk_12);
  const auto& family = calculator::displayFontFamily;
  assert(family.getData(EpdFontFamily::REGULAR) == &notosans_18_regular);
  assert(family.getData(EpdFontFamily::BOLD) == &notosans_18_bold);
  assert(family.getData(EpdFontFamily::REGULAR)->advanceY == 51);
  std::printf("Digit height: %u -> %u px\n", previousFont.getGlyph('0')->height, family.getGlyph('0')->height);
  for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
    for (char digit = '0'; digit <= '9'; ++digit) {
      assert(family.hasCodepoint(digit, style));
      assert(family.getGlyph(digit, style)->height > previousFont.getGlyph(digit)->height);
    }
    for (const uint32_t symbol : std::initializer_list<uint32_t>{'+', '-', '.', '%', '=', 'e', ' ', 0xD7, 0xF7}) {
      assert(family.hasCodepoint(symbol, style));
    }
    for (const char* text : {"0", "123456789012", "-123.456", "123456789012 + 987654321098 =", "1.23456789e+12"}) {
      int width = 0, height = 0;
      family.getTextDimensions(text, &width, &height, style);
      assert(width > 0 && height > 0 && height <= 51);
      std::printf("%s: %d x %d px (%s)\n", text, width, height, style == EpdFontFamily::BOLD ? "bold" : "regular");
    }
  }
}
