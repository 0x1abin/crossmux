#include "FontCacheManager.h"

#include <FontDecompressor.h>
#include <Logging.h>
#include <SdCardFont.h>

#include <cstring>
#include <iterator>

#ifdef ENABLE_CHINESE_VERSION
namespace {
constexpr bool isDownloadableChineseCodepoint(const uint32_t cp) {
  return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF);
}

static_assert(isDownloadableChineseCodepoint(0x4E00));
static_assert(isDownloadableChineseCodepoint(0xFAFF));
static_assert(!isDownloadableChineseCodepoint(0x4DFF));
static_assert(!isDownloadableChineseCodepoint(0xFB00));
}  // namespace
#endif

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

void FontCacheManager::releaseSdFontCaches() {
  for (auto& [id, font] : sdCardFonts_) {
    font->releaseResidentCaches();
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

bool FontCacheManager::canIdlePrewarm(const int fontId) const {
  return sdCardFonts_.find(fontId) != sdCardFonts_.end();
}

bool FontCacheManager::needsPrewarmScan(const int fontId) const {
  if (sdCardFonts_.count(fontId) != 0) return true;

  const auto familyIt = fontMap_.find(fontId);
  if (familyIt == fontMap_.end()) return false;
  for (uint8_t i = 0; i < 4; i++) {
    const auto style = static_cast<EpdFontFamily::Style>(i);
    const EpdFontData* data = familyIt->second.getData(style);
    if (data && data->groups) return true;
  }
  return false;
}

bool FontCacheManager::isScanning() const { return scanMode_ == ScanMode::Scanning; }

void FontCacheManager::recordText(const char* text, const int fontId, EpdFontFamily::Style style) {
  scanText_ += text;
  if (scanFontId_ < 0) scanFontId_ = fontId;
  if (scanFontIdCount_ < static_cast<int>(std::size(scanFontIds_))) {
    bool known = false;
    for (int i = 0; i < scanFontIdCount_; i++) {
      if (scanFontIds_[i] == fontId) {
        known = true;
        break;
      }
    }
    if (!known) scanFontIds_[scanFontIdCount_++] = fontId;
  }
  const uint8_t baseStyle = static_cast<uint8_t>(style) & 0x03;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
  uint32_t cpCount = 0;
  while (*p) {
    if ((*p & 0xC0) != 0x80) cpCount++;
    p++;
  }
  scanStyleCounts_[baseStyle] += cpCount;
}

#ifdef ENABLE_CHINESE_VERSION
void FontCacheManager::reportMissingChineseCodepoint(const int fontId, const uint32_t codepoint) {
  if (missingChineseCodepoint_ != 0 || sdCardFonts_.count(fontId) != 0 || !isDownloadableChineseCodepoint(codepoint)) {
    return;
  }
  missingChineseCodepoint_ = codepoint;
}

uint32_t FontCacheManager::consumeMissingChineseCodepoint() {
  const uint32_t codepoint = missingChineseCodepoint_;
  missingChineseCodepoint_ = 0;
  return codepoint;
}
#endif

// --- PrewarmScope implementation ---

FontCacheManager::PrewarmScope::PrewarmScope(FontCacheManager& manager) : manager_(&manager) {
  manager_->scanMode_ = ScanMode::Scanning;
  manager_->clearCache();
  manager_->resetStats();
  manager_->scanText_.clear();
  manager_->scanText_.reserve(2048);  // Pre-allocate to avoid heap fragmentation from repeated concat
  memset(manager_->scanStyleCounts_, 0, sizeof(manager_->scanStyleCounts_));
  manager_->scanFontId_ = -1;
  manager_->scanFontIdCount_ = 0;
  for (int i = 0; i < static_cast<int>(std::size(manager_->scanFontIds_)); i++) {
    manager_->scanFontIds_[i] = -1;
  }
#ifdef ENABLE_CHINESE_VERSION
  manager_->missingChineseCodepoint_ = 0;
#endif
}

void FontCacheManager::PrewarmScope::endScanAndPrewarm() {
  manager_->scanMode_ = ScanMode::None;
  if (manager_->scanText_.empty()) return;

  // Build style bitmask from all styles that appeared during the scan
  uint8_t styleMask = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (manager_->scanStyleCounts_[i] > 0) styleMask |= (1 << i);
  }
  if (styleMask == 0) styleMask = 1;  // default to regular

  // Prewarm every distinct font seen during the scan: a UI font that drew
  // before the body text (status bar, placeholders) must not steal the whole
  // prewarm; the reader font carrying the page text is among the recorded ids.
  if (manager_->scanFontIdCount_ == 0 && manager_->scanFontId_ >= 0) {
    manager_->scanFontIds_[0] = manager_->scanFontId_;
    manager_->scanFontIdCount_ = 1;
  }
  for (int i = 0; i < manager_->scanFontIdCount_; i++) {
    const int fontId = manager_->scanFontIds_[i];
    if (fontId == -1) continue;  // unused slot; SD font ids are signed FNV hashes and may be negative
    manager_->prewarmCache(fontId, manager_->scanText_.c_str(), styleMask);
  }

  // Free scan string memory
  manager_->scanText_.clear();
  manager_->scanText_.shrink_to_fit();
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
