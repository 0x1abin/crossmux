#include "FontCacheManager.h"

#include <FontDecompressor.h>
#include <Logging.h>
#include <SdCardFont.h>

#include <cstring>

FontCacheManager::FontCacheManager(const std::map<int, EpdFontFamily>& fontMap,
                                   const std::map<int, SdCardFont*>& sdCardFonts)
    : fontMap_(fontMap), sdCardFonts_(sdCardFonts) {}

void FontCacheManager::setFontDecompressor(FontDecompressor* d) { fontDecompressor_ = d; }

void FontCacheManager::clearCache() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
  for (auto& [id, font] : sdCardFonts_) {
    font->clearCache();
  }
}

void FontCacheManager::prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask) {
  // SD card font prewarm path: prewarm all requested styles in one call
  auto it = sdCardFonts_.find(fontId);
  if (it != sdCardFonts_.end()) {
    int missed = it->second->prewarm(utf8Text, styleMask);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache(SD): %d glyph(s) not found (styleMask=0x%02X)", missed, styleMask);
    }
    return;
  }

  // Standard compressed font prewarm path: loop over all requested styles
  if (!fontDecompressor_ || fontMap_.count(fontId) == 0) return;

  for (uint8_t i = 0; i < 4; i++) {
    if (!(styleMask & (1 << i))) continue;
    auto style = static_cast<EpdFontFamily::Style>(i);
    const EpdFontData* data = fontMap_.at(fontId).getData(style);
    if (!data || !data->groups) continue;
    int missed = fontDecompressor_->prewarmCache(data, utf8Text);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache: %d glyph(s) not cached for style %d", missed, i);
    }
  }
}

void FontCacheManager::logStats(const char* label) {
  if (fontDecompressor_) fontDecompressor_->logStats(label);
  for (auto& [id, font] : sdCardFonts_) {
    font->logStats(label);
  }
}

void FontCacheManager::resetStats() {
  if (fontDecompressor_) fontDecompressor_->resetStats();
  for (auto& [id, font] : sdCardFonts_) {
    font->resetStats();
  }
}

bool FontCacheManager::isScanning() const { return scanMode_ == ScanMode::Scanning; }

void FontCacheManager::recordText(const char* text, int fontId, EpdFontFamily::Style style) {
  if (!text) return;
  // Bucket the text under its base style so each style is prewarmed only for what
  // it actually renders (see the header comment on scanText_).
  const uint8_t baseStyle = static_cast<uint8_t>(style) & 0x03;
  size_t& len = scanTextLen_[baseStyle];
  const size_t remaining = (len < SCAN_TEXT_CAPACITY - 1) ? (SCAN_TEXT_CAPACITY - 1 - len) : 0;
  if (remaining > 0) {
    const size_t textLen = strnlen(text, remaining);
    memcpy(scanText_[baseStyle] + len, text, textLen);
    len += textLen;
    scanText_[baseStyle][len] = '\0';
  }
  if (scanFontId_ < 0) scanFontId_ = fontId;
}

// --- PrewarmScope implementation ---

FontCacheManager::PrewarmScope::PrewarmScope(FontCacheManager& manager) : manager_(&manager) {
  manager_->scanMode_ = ScanMode::Scanning;
  manager_->clearCache();
  manager_->resetStats();
  for (uint8_t i = 0; i < STYLE_COUNT; i++) {
    manager_->scanTextLen_[i] = 0;
    manager_->scanText_[i][0] = '\0';
  }
  manager_->scanFontId_ = -1;
}

void FontCacheManager::PrewarmScope::endScanAndPrewarm() {
  manager_->scanMode_ = ScanMode::None;
  if (manager_->scanFontId_ < 0) return;  // nothing recorded (also the "already ran" no-op)

  // Prewarm each style separately, against only the text drawn in that style, so a
  // secondary style's page-slot buffer stays as small as its actual usage. A single
  // per-style bit mask routes prewarmCache() to exactly that style on both the SD
  // and compressed-font paths.
  for (uint8_t i = 0; i < STYLE_COUNT; i++) {
    if (manager_->scanTextLen_[i] == 0) continue;
    manager_->prewarmCache(manager_->scanFontId_, manager_->scanText_[i], static_cast<uint8_t>(1u << i));
    manager_->scanTextLen_[i] = 0;
    manager_->scanText_[i][0] = '\0';
  }
  manager_->scanFontId_ = -1;  // makes the dtor's second call a no-op
}

FontCacheManager::PrewarmScope::~PrewarmScope() {
  if (active_) {
    endScanAndPrewarm();  // no-op if already called (scanText_ is empty)
    manager_->clearCache();
  }
}

FontCacheManager::PrewarmScope::PrewarmScope(PrewarmScope&& other) noexcept
    : manager_(other.manager_), active_(other.active_) {
  other.active_ = false;
}

FontCacheManager::PrewarmScope FontCacheManager::createPrewarmScope() { return PrewarmScope(*this); }
