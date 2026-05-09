#pragma once

#include "I18nKeys.h"

class GfxRenderer;

// Re-registers UI_10_FONT_ID / UI_12_FONT_ID / SMALL_FONT_ID with the font
// family that matches the active locale. ZH switches to a Noto Sans SC subset
// built from the strings in lib/I18n/translations/simplified_chinese.yaml;
// every other language uses the Latin Ubuntu / Noto Sans family registered
// at boot in main.cpp. Safe to call before initial registration and safe to
// call repeatedly --- GfxRenderer::insertFont upserts.
void applyUiFontForLanguage(GfxRenderer& renderer, Language language);
