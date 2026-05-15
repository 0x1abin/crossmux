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
// Obfuscation utils (lib/Serialization/ObfuscationUtils excluded)
// =============================================================================

#include "ObfuscationUtils.h"

namespace obfuscation {
void xorTransform(std::string&) {}
void xorTransform(std::string&, const uint8_t*, size_t) {}
String obfuscateToBase64(const std::string&) { return String(); }
std::string deobfuscateFromBase64(const char*, bool* ok) {
  if (ok) *ok = false;
  return std::string();
}
void selfTest() {}
}  // namespace obfuscation

// =============================================================================
// QrUtils (excluded — relies on qrcode library)
// =============================================================================

#include "util/QrUtils.h"

namespace QrUtils {
void drawQrCode(const GfxRenderer&, const Rect&, const std::string&) {}
}

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

#include "activities/settings/FontDownloadActivity.h"
#include "SdCardFontSystem.h"
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
