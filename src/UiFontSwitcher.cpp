#include "UiFontSwitcher.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include "builtinFonts/notosans_8_regular.h"
#include "builtinFonts/notosans_sc_10_bold.h"
#include "builtinFonts/notosans_sc_10_regular.h"
#include "builtinFonts/notosans_sc_12_bold.h"
#include "builtinFonts/notosans_sc_12_regular.h"
#include "builtinFonts/notosans_sc_8_regular.h"
#include "builtinFonts/ubuntu_10_bold.h"
#include "builtinFonts/ubuntu_10_regular.h"
#include "builtinFonts/ubuntu_12_bold.h"
#include "builtinFonts/ubuntu_12_regular.h"
#include "fontIds.h"

namespace {

// Latin UI families (used by every non-ZH locale).
EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// Simplified Chinese subset (built from translation YAMLs).
EpdFont sc8RegularFont(&notosans_sc_8_regular);
EpdFontFamily sc8FontFamily(&sc8RegularFont);

EpdFont sc10RegularFont(&notosans_sc_10_regular);
EpdFont sc10BoldFont(&notosans_sc_10_bold);
EpdFontFamily sc10FontFamily(&sc10RegularFont, &sc10BoldFont);

EpdFont sc12RegularFont(&notosans_sc_12_regular);
EpdFont sc12BoldFont(&notosans_sc_12_bold);
EpdFontFamily sc12FontFamily(&sc12RegularFont, &sc12BoldFont);

}  // namespace

void applyUiFontForLanguage(GfxRenderer& renderer, Language language) {
  if (language == Language::ZH) {
    renderer.insertFont(SMALL_FONT_ID, sc8FontFamily);
    renderer.insertFont(UI_10_FONT_ID, sc10FontFamily);
    renderer.insertFont(UI_12_FONT_ID, sc12FontFamily);
  } else {
    renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
    renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
    renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  }
}
