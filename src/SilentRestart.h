#pragma once

#include <cstdint>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

enum class SilentRestartTarget : uint8_t {
  Home,
  HomeOpds,
  HomeFileTransfer,
  SettingsCheckUpdates,
  SettingsManageFonts,
  SettingsText,
  SettingsDateTime,
  SettingsKOReader,
  Reader,
  Count,
};

constexpr SilentRestartTarget decodeSilentRestartTarget(const uint32_t rawTarget) {
  return rawTarget < static_cast<uint32_t>(SilentRestartTarget::Count) ? static_cast<SilentRestartTarget>(rawTarget)
                                                                       : SilentRestartTarget::Home;
}

static_assert(decodeSilentRestartTarget(static_cast<uint32_t>(SilentRestartTarget::Reader)) ==
              SilentRestartTarget::Reader);
static_assert(decodeSilentRestartTarget(static_cast<uint32_t>(SilentRestartTarget::Count)) ==
              SilentRestartTarget::Home);

void silentRestart(SilentRestartTarget target);
