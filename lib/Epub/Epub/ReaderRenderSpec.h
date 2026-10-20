#pragma once
#include <cstdint>

#include "FirstLineIndent.h"

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on every field: a section file built
// with a different spec is discarded and rebuilt.
//
// Build one via CrossPointSettings::readerRenderSpec(width, height), which
// fills settings and viewport fields. The reader supplies collectTouchLinks
// from the input device and applies any session-only CSS fallback. Taking
// the viewport as arguments is what keeps a spec from
// existing in a half-filled state — the 0 defaults below are a last-resort
// backstop (a 0x0 viewport lays out nothing), not an invitation to omit it.
struct ReaderRenderSpec {
  int fontId = 0;
  float lineCompression = 1.0f;
  uint8_t extraParagraphSpacing = 0;  // 0=off, 1..5=0.5x/0.75x/1x/1.25x/1.5x line height
  // Reader-level first-line indent, one of FirstLineIndent::Auto/Indent/
  // NoIndent (see ParsedText.h). Independent of extraParagraphSpacing:
  // paragraphs keep their indent even when extra paragraph spacing is
  // enabled.
  uint8_t firstLineIndent = FirstLineIndent::Auto;
  uint8_t paragraphIndentSpaces = 3;
  int8_t characterSpacing = 0;
  uint8_t wordSpacingPercent = 100;
  uint8_t paragraphAlignment = 0;
  // Vertical bottom-align: redistributes each non-final page's text lines so
  // the last line reaches the content bottom with even spacing. Keyed into the
  // section cache so a toggle change rebuilds cached pages.
  bool verticalBottomAlign = false;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
  bool collectTouchLinks = false;

  bool operator==(const ReaderRenderSpec& other) const {
    return fontId == other.fontId && lineCompression == other.lineCompression &&
           extraParagraphSpacing == other.extraParagraphSpacing && firstLineIndent == other.firstLineIndent &&
           paragraphIndentSpaces == other.paragraphIndentSpaces && characterSpacing == other.characterSpacing &&
           wordSpacingPercent == other.wordSpacingPercent && paragraphAlignment == other.paragraphAlignment &&
           verticalBottomAlign == other.verticalBottomAlign && viewportWidth == other.viewportWidth &&
           viewportHeight == other.viewportHeight && hyphenationEnabled == other.hyphenationEnabled &&
           embeddedStyle == other.embeddedStyle && imageRendering == other.imageRendering &&
           focusReadingEnabled == other.focusReadingEnabled && collectTouchLinks == other.collectTouchLinks;
  }
};

// Stable signature for clipping anchors. Page-local word ordinals are valid
// only for the exact layout that produced them.
inline uint32_t readerRenderSpecSignature(const ReaderRenderSpec& spec) {
  uint32_t signature = 2166136261U;
  const auto mix = [&signature](const uint32_t value) {
    signature ^= value;
    signature *= 16777619U;
  };
  mix(static_cast<uint32_t>(spec.fontId));
  mix(static_cast<uint32_t>(spec.lineCompression * 1000.0f));
  mix(spec.extraParagraphSpacing);
  mix(spec.firstLineIndent);
  mix(spec.paragraphIndentSpaces);
  mix(static_cast<uint8_t>(spec.characterSpacing));
  mix(spec.wordSpacingPercent);
  mix(spec.paragraphAlignment);
  mix(spec.viewportWidth);
  mix(spec.viewportHeight);
  mix(spec.hyphenationEnabled);
  mix(spec.embeddedStyle);
  mix(spec.imageRendering);
  mix(spec.focusReadingEnabled);
  mix(spec.collectTouchLinks);
  // Preserve disabled signatures; the high bit also stays distinct from reserved 0/1.
  if (spec.verticalBottomAlign) signature ^= 0x80000000u;
  return signature == 0 ? 1 : signature;
}
