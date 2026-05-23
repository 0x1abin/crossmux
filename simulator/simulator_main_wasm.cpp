// Simulator entry point for the WebAssembly (Emscripten) build.
//
// Unlike simulator_main.cpp (which drives an SDL window + event loop), the WASM build
// has no window: rendering and input are owned by the browser (see crosspoint-web
// public/simulator/index.html). main() only:
//   1. points HalStorage at the preloaded virtual SD root ("/sd"),
//   2. spawns the firmware setup()/loop() on a FreeRTOS task (== Emscripten pthread),
//   3. hands control back to the browser event loop, keeping the runtime alive.
//
// setup()/loop() are renamed to firmware_setup()/firmware_loop() via the CMake
// -Dsetup=firmware_setup -Dloop=firmware_loop flags, same as the native simulator.

#include <CrossPointSettings.h>
#include <I18n.h>

#include <cstring>
#include <string>

#include "simulator_firmware.h"

// Consumed by hal/HalStorage.cpp (reused as-is). All firmware SD paths resolve under this root;
// the demo book + .crosspoint cache are preloaded into MEMFS at "/sd" via the build's
// --preload-file.
std::string g_simulator_sd_root;

int main(int argc, char** argv) {
  g_simulator_sd_root = "/sd";

  // Startup UI language follows the browser: index.html maps navigator.language to a
  // "--lang <CODE>" arg (CODE = "ZH_CN" / "EN"). Seed SETTINGS.language BEFORE the
  // firmware task runs. firmware setup() calls loadFromFile() then
  // I18N.setLanguage(SETTINGS.language); loadFromFile() leaves SETTINGS.language
  // untouched when /sd/.crosspoint/settings.json is absent (the fresh-MEMFS demo case),
  // so this seed is what the home screen renders in. A persisted settings.json (e.g.
  // future IDBFS) would still override it — user choice > browser default.
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], "--lang") == 0) {
      CrossPointSettings::getInstance().language = static_cast<uint8_t>(I18n::languageFromCode(argv[i + 1]));
      break;
    }
  }

  // Spawn the firmware setup()+loop() task (see simulator_firmware.h; on Emscripten it maps to a
  // Web Worker that may block on notifications/mutexes freely).
  simulator::startFirmwareTask();

  // Returning from main() hands control back to the browser event loop. With
  // -sEXIT_RUNTIME=0 the runtime is NOT torn down, so the firmware worker (and the
  // ActivityManager render task it spawns) keep running and the exported
  // input/framebuffer functions stay callable. (We avoid
  // emscripten_exit_with_live_runtime() because it unwinds via a thrown exception,
  // which does not mix with the -fno-exceptions build.)
  return 0;
}
