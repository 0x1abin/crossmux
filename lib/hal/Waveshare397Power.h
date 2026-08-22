#pragma once

#include <cstdint>

namespace Waveshare397Power {

class PowerKeyState {
 public:
  // AXP2101 PKEY is active-low: the negative edge is press and the
  // positive edge is release in InterruptStatus2.
  static constexpr uint8_t PRESS_IRQ = 1u << 1;
  static constexpr uint8_t RELEASE_IRQ = 1u << 0;

  bool apply(uint8_t irqStatus) {
    const auto event = static_cast<Event>(irqStatus & (PRESS_IRQ | RELEASE_IRQ));
    switch (event) {
      case Event::None:
        if (phase_ == Phase::PendingRelease) phase_ = Phase::Released;
        break;
      case Event::Release:
        phase_ = Phase::Released;
        break;
      case Event::Press:
        phase_ = Phase::Pressed;
        break;
      case Event::PressAndRelease:
        phase_ = Phase::PendingRelease;
        return true;
    }
    return current();
  }

  bool current() const { return phase_ != Phase::Released; }

 private:
  enum class Event : uint8_t {
    None = 0,
    Release = RELEASE_IRQ,
    Press = PRESS_IRQ,
    PressAndRelease = PRESS_IRQ | RELEASE_IRQ,
  };
  enum class Phase : uint8_t { Released, Pressed, PendingRelease };

  Phase phase_ = Phase::Released;
};

bool begin();
bool powerButtonPressed();
void waitForPowerButtonRelease();
bool setDisplayPower(bool enabled);
bool readBatteryPercentage(uint16_t& percent);
bool externalPowerConnected(bool& connected);
bool shutdown();

}  // namespace Waveshare397Power
