// Simulator entry point. Renames Arduino's setup/loop → firmware_setup/firmware_loop
// (via CMake -Dsetup=firmware_setup -Dloop=firmware_loop), then drives them from the
// SDL main loop.

#include <HalGPIO.h>
#include <SDL.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "simulator_config.h"
#include "simulator_settings.h"
#include "simulator_window.h"

// Provided by src/main.cpp (renamed via build flags).
void firmware_setup();
void firmware_loop();

namespace {
void firmwareTask(void* /*unused*/) {
  firmware_setup();
  while (true) {
    firmware_loop();
  }
}
}  // namespace

namespace {

struct CliArgs {
  std::string sdRoot = SIMULATOR_DEFAULT_SD_ROOT;
  int scale = SIMULATOR_WINDOW_SCALE;
};

CliArgs parse_args(int argc, char** argv) {
  CliArgs args;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--sd-root" && i + 1 < argc) {
      args.sdRoot = argv[++i];
    } else if (a == "--scale" && i + 1 < argc) {
      args.scale = std::atoi(argv[++i]);
    } else if (a == "--help" || a == "-h") {
      std::printf("Usage: %s [--sd-root <dir>] [--scale <N>]\n", argv[0]);
      std::exit(0);
    }
  }
  return args;
}

uint8_t map_key_to_button(SDL_Keycode key) {
  switch (key) {
    case SDLK_BACKSPACE: return HalGPIO::BTN_BACK;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return HalGPIO::BTN_CONFIRM;
    case SDLK_LEFT:     return HalGPIO::BTN_LEFT;
    case SDLK_RIGHT:    return HalGPIO::BTN_RIGHT;
    case SDLK_UP:       return HalGPIO::BTN_UP;
    case SDLK_DOWN:     return HalGPIO::BTN_DOWN;
    case SDLK_ESCAPE:   return HalGPIO::BTN_POWER;
    default:            return 0xFF;
  }
}

}  // namespace

namespace simulator {
// Defined in HalGPIO_native.cpp.
void injectButton(uint8_t buttonIndex, bool down);
}

// Make the simulator SD-root path available to HalStorage_native.cpp via a global.
std::string g_simulator_sd_root;

int main(int argc, char** argv) {
  CliArgs args = parse_args(argc, argv);
  g_simulator_sd_root = args.sdRoot;

  simulator::HostSettings hostSettings;  // defaults from HostSettings struct
  simulator::loadHostSettings(hostSettings);

  // Don't let SDL install its own SIGTERM/SIGINT handlers. By default SDL2 catches
  // them, posts SDL_QUIT, then returns control to us — which then walks out through
  // static destruction and trips ~ActivityManager()'s assert(false). With this hint
  // set, SIGTERM terminates the process directly (the kernel default) and never runs
  // C++ static destructors.
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  if (!simulator::SimulatorWindow::instance().open("CrossPoint Simulator", args.scale,
                                                   hostSettings.showDeviceShell)) {
    SDL_Quit();
    return 1;
  }
  simulator::SimulatorWindow::instance().settingsOverlay().syncFrom(hostSettings);

  // Run the firmware setup + loop on a registered FreeRTOS task so that
  // xTaskGetCurrentTaskHandle() returns a non-null handle inside firmware code —
  // ActivityManager's RenderLock-holder assertion relies on that. The main thread
  // owns the SDL window/event loop.
  xTaskCreate(&firmwareTask, "firmware", 8192, nullptr, 1, nullptr);

  bool running = true;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_QUIT:
          running = false;
          break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          if (ev.key.repeat) break;  // Ignore key-repeat: HalGPIO is edge-triggered.
          if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F1) {
            auto& overlay = simulator::SimulatorWindow::instance().settingsOverlay();
            overlay.setVisible(!overlay.isVisible());
            simulator::SimulatorWindow::instance().forceRedraw();
            break;
          }
          uint8_t btn = map_key_to_button(ev.key.keysym.sym);
          if (btn != 0xFF) {
            simulator::injectButton(btn, ev.type == SDL_KEYDOWN);
          }
          break;
        }
        case SDL_MOUSEBUTTONDOWN: {
          if (ev.button.button != SDL_BUTTON_LEFT) break;
          auto& w = simulator::SimulatorWindow::instance();
          if (!w.settingsOverlay().isVisible()) break;
          if (w.settingsOverlay().handleClick(ev.button.x, ev.button.y)) {
            hostSettings.showDeviceShell = !hostSettings.showDeviceShell;
            simulator::saveHostSettings(hostSettings);
            w.setShellVisible(hostSettings.showDeviceShell);  // resizes + redraws
          }
          break;
        }
        default:
          break;
      }
    }
    simulator::SimulatorWindow::instance().presentIfDirty();
    SDL_Delay(16);  // ~60 fps event pump.
  }

  simulator::SimulatorWindow::instance().close();
  SDL_Quit();
  // Use std::_Exit (not std::exit) to skip C++ static destructors. Several firmware
  // singletons — most notably ActivityManager — have destructors that assert(false)
  // because they're not designed for clean shutdown (the device only ever resets).
  std::_Exit(0);
}
