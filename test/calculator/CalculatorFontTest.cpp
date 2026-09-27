#include <cassert>
#include <cstdio>
#include <initializer_list>

#include "CalculatorFont.h"
#include "builtinFonts/notosans_18_bold.h"
#include "builtinFonts/notosans_18_regular.h"
#include "builtinFonts/notosans_cjk_12.h"

int main() {
  const EpdFont previousFont(&notosans_cjk_12);
  const auto& family = calculator::displayFontFamily;
  assert(family.getData(EpdFontFamily::REGULAR) == &calculator_18_regular);
  assert(family.getData(EpdFontFamily::BOLD) == &calculator_18_bold);
  assert(family.getData(EpdFontFamily::REGULAR)->advanceY == 51);
  std::printf("Digit height: %u -> %u px\n", previousFont.getGlyph('0')->height, family.getGlyph('0')->height);
  for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
    const auto* originalData = style == EpdFontFamily::REGULAR ? &notosans_18_regular : &notosans_18_bold;
    const EpdFont original(originalData);
    const auto* subsetData = family.getData(style);
    assert(subsetData->advanceY == originalData->advanceY);
    assert(subsetData->ascender == originalData->ascender);
    assert(subsetData->descender == originalData->descender);
    constexpr uint32_t characters[] = {'0', '1', '2', '3',  '4',  '5', '6', '7', '8', '9',
                                       ' ', '+', '-', 0xD7, 0xF7, '.', '%', '=', 'e', 0xFFFD};
    for (const uint32_t cp : characters) {
      assert(family.hasCodepoint(cp, style));
      const auto* before = original.getGlyph(cp);
      const auto* after = family.getGlyph(cp, style);
      assert(after->width == before->width && after->height == before->height);
      assert(after->advanceX == before->advanceX && after->left == before->left && after->top == before->top);
      for (const uint32_t next : characters) {
        assert(family.getKerning(cp, next, style) == original.getKerning(cp, next));
      }
    }
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
