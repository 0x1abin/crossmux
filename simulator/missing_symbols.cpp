// Symbol stubs for code paths that the simulator does not exercise.
//
// Several .cpp files from the firmware tree are excluded from the simulator build
// because they pull in heavy dependencies (real PNG/JPEG decoders, ESPmDNS, finely-
// typed WiFi APIs). The classes they define are still referenced from elsewhere —
// notably ActivityManager's goTo*() methods that std::make_unique<X>() the activity,
// pulling in X's vtable. To satisfy the linker we provide empty out-of-line
// definitions for every virtual method declared by those classes, plus free functions
// and class statics that other reachable code names.
//
// None of these stubs are intended to be called at runtime. If the user navigates to
// a screen that needs one (a WiFi prompt, OTA, image rendering), the stub will
// silently return failure/empty values — fine for the first-version simulator scope
// (boot → home → file browse → EPUB text without images).

#include <HalStorage.h>  // FsFile (alias of HalFile)
#include <Logging.h>
#include <WiFi.h>

#include <cstring>
#include <memory>

// =============================================================================
// Globals for shimmed Arduino-ESP32 ecosystem libraries
// =============================================================================

WiFiClass WiFi;

// =============================================================================
// MySerialImpl + uzlib
// =============================================================================

size_t MySerialImpl::printf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  size_t n = logSerial.vprintf(format, ap);
  va_end(ap);
  return n;
}
size_t MySerialImpl::write(uint8_t b) { return logSerial.write(b); }
size_t MySerialImpl::write(const uint8_t* buffer, size_t size) { return logSerial.write(buffer, size); }
void MySerialImpl::flush() { logSerial.flush(); }
MySerialImpl MySerialImpl::instance;

extern "C" {
unsigned int uzlib_adler32(const void*, unsigned int, unsigned int prev) { return prev; }
unsigned int uzlib_crc32(const void*, unsigned int, unsigned int prev) { return prev; }
}

// =============================================================================
// Excluded image converters / decoder factory
// =============================================================================

#include "PngToBmpConverter.h"

bool PngToBmpConverter::pngFileToBmpStream(FsFile&, Print&, bool) { return false; }
bool PngToBmpConverter::pngFileToBmpStreamWithSize(FsFile&, Print&, int, int) { return false; }
bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(FsFile&, Print&, int, int) { return false; }

#include "JpegToBmpConverter.h"

bool JpegToBmpConverter::jpegFileToBmpStream(FsFile&, Print&, bool) { return false; }
bool JpegToBmpConverter::jpegFileToBmpStreamWithSize(FsFile&, Print&, int, int) { return false; }
bool JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(FsFile&, Print&, int, int) { return false; }

#include "Epub/converters/ImageDecoderFactory.h"

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

// =============================================================================
// Obfuscation utils (lib/Serialization/ObfuscationUtils excluded — pulls esp_mac.h
// and mbedtls). Simulator persists secrets to the host SD root, so we round-trip
// through plain base64. The XOR step is a no-op (no hardware MAC available);
// secrecy of dev-machine SD card contents is the host user's responsibility.
// =============================================================================

#include "ObfuscationUtils.h"

namespace {
constexpr char kB64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode_(const std::string& in) {
  std::string out;
  const size_t n = in.size();
  out.reserve(((n + 2) / 3) * 4);
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t b0 = static_cast<uint8_t>(in[i]);
    const uint32_t b1 = (i + 1 < n) ? static_cast<uint8_t>(in[i + 1]) : 0;
    const uint32_t b2 = (i + 2 < n) ? static_cast<uint8_t>(in[i + 2]) : 0;
    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out.push_back(kB64Alphabet[(triple >> 18) & 0x3f]);
    out.push_back(kB64Alphabet[(triple >> 12) & 0x3f]);
    out.push_back(i + 1 < n ? kB64Alphabet[(triple >> 6) & 0x3f] : '=');
    out.push_back(i + 2 < n ? kB64Alphabet[triple & 0x3f] : '=');
  }
  return out;
}

bool base64Decode_(const char* in, std::string& out) {
  static int8_t table[256];
  static bool tableInit = false;
  if (!tableInit) {
    for (int i = 0; i < 256; ++i) table[i] = -1;
    for (int i = 0; i < 64; ++i) table[static_cast<uint8_t>(kB64Alphabet[i])] = static_cast<int8_t>(i);
    tableInit = true;
  }
  out.clear();
  if (!in) return false;
  uint32_t buf = 0;
  int bits = 0;
  for (const char* p = in; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
      if (c == '=') break;
      continue;
    }
    const int v = table[c];
    if (v < 0) return false;
    buf = (buf << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buf >> bits) & 0xff));
    }
  }
  return true;
}
}  // namespace

namespace obfuscation {
void xorTransform(std::string&) {}
void xorTransform(std::string&, const uint8_t*, size_t) {}
String obfuscateToBase64(const std::string& plaintext) {
  if (plaintext.empty()) return String();
  return String(base64Encode_(plaintext).c_str());
}
std::string deobfuscateFromBase64(const char* encoded, bool* ok) {
  if (!encoded || encoded[0] == '\0') {
    if (ok) *ok = false;
    return std::string();
  }
  std::string decoded;
  if (!base64Decode_(encoded, decoded)) {
    if (ok) *ok = false;
    return std::string();
  }
  if (ok) *ok = true;
  return decoded;
}
void selfTest() {}
}  // namespace obfuscation

// =============================================================================
// ProgressMapper (KOReader sync excluded except credential store)
// =============================================================================

#include "ProgressMapper.h"

KOReaderPosition ProgressMapper::toKOReader(const std::shared_ptr<Epub>&, const CrossPointPosition&) {
  return KOReaderPosition{};
}

// =============================================================================
// Excluded activity vtables
//
// Each block provides empty out-of-line definitions for the activity's overridden
// virtuals. Constructors that are declared but not inlined in the header also need
// to be supplied.
// =============================================================================

#define STUB_ACTIVITY_BASE(Cls) \
  void Cls::onEnter() {}        \
  void Cls::onExit() {}         \
  void Cls::loop() {}           \
  void Cls::render(RenderLock&&) {}

#include "activities/network/WifiSelectionActivity.h"
STUB_ACTIVITY_BASE(WifiSelectionActivity)

#include "activities/network/CrossPointWebServerActivity.h"
STUB_ACTIVITY_BASE(CrossPointWebServerActivity)

#include "activities/network/CalibreConnectActivity.h"
STUB_ACTIVITY_BASE(CalibreConnectActivity)

#include "activities/browser/OpdsBookBrowserActivity.h"
STUB_ACTIVITY_BASE(OpdsBookBrowserActivity)

#include "activities/settings/OtaUpdateActivity.h"
STUB_ACTIVITY_BASE(OtaUpdateActivity)

#include "activities/settings/SdFirmwareUpdateActivity.h"
void SdFirmwareUpdateActivity::onEnter() {}
void SdFirmwareUpdateActivity::loop() {}
void SdFirmwareUpdateActivity::render(RenderLock&&) {}
void SdFirmwareUpdateActivity::launchPicker() {}
void SdFirmwareUpdateActivity::onPickerResult(const ActivityResult&) {}
bool SdFirmwareUpdateActivity::validateFirmware() { return false; }
void SdFirmwareUpdateActivity::promptConfirmation() {}
void SdFirmwareUpdateActivity::onConfirmationResult(const ActivityResult&) {}
void SdFirmwareUpdateActivity::performUpdate() {}

#include "SdCardFontSystem.h"
#include "activities/settings/FontDownloadActivity.h"
FontDownloadActivity::FontDownloadActivity(GfxRenderer& r, MappedInputManager& m)
    : Activity("FontDownload", r, m), fontInstaller_(sdFontSystem.registry()) {}
STUB_ACTIVITY_BASE(FontDownloadActivity)

#include "activities/settings/KOReaderAuthActivity.h"
STUB_ACTIVITY_BASE(KOReaderAuthActivity)

#include "activities/settings/KOReaderSettingsActivity.h"
STUB_ACTIVITY_BASE(KOReaderSettingsActivity)

#include "activities/reader/KOReaderSyncActivity.h"
STUB_ACTIVITY_BASE(KOReaderSyncActivity)

// CrossPointWebServer destructor referenced from CrossPointWebServerActivity vtable.
#include "network/CrossPointWebServer.h"
CrossPointWebServer::~CrossPointWebServer() = default;

#undef STUB_ACTIVITY_BASE
