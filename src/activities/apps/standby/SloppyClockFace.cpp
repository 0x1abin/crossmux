#include "SloppyClockFace.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>
#include <time.h>

#include <cstdio>

#include "StandbyTime.h"

void SloppyClockFace::onEnter() {
  style_ = makeUniqueNoThrow<sloppy::Style>();
  seeds_ = makeUniqueNoThrow<sloppy::Seeds>();
  if (!style_ || !seeds_) {
    LOG_ERR("STANDBY", "OOM allocating sloppy clock state");
    return;  // tick()/render() will see nullptr and skip work
  }
  startMs_ = millis();
  lastMin_ = -1;
  regenerate(esp_random() ^ static_cast<uint32_t>(startMs_));
}

void SloppyClockFace::onExit() {
  seeds_.reset();
  style_.reset();
}

void SloppyClockFace::onShake(uint32_t seed) {
  if (!style_ || !seeds_) return;
  regenerate(seed);
}

void SloppyClockFace::onPagePrev() {
  onShake(esp_random() ^ static_cast<uint32_t>(millis()));
}

void SloppyClockFace::onPageNext() {
  onShake(esp_random() ^ static_cast<uint32_t>(millis()));
}

void SloppyClockFace::regenerate(uint32_t seed) {
  sloppy::rollStyle(seed, *style_);
  sloppy::preRollSeeds(seed, sloppy::getAlphabet(style_->alphabet), *seeds_);
  lastMin_ = -1;
}

bool SloppyClockFace::tick() {
  if (!style_ || !seeds_) return false;
  const uint32_t nowMin = standby_time::getMinuteTick(startMs_);
  if (static_cast<int32_t>(nowMin) == lastMin_) return false;
  lastMin_ = static_cast<int32_t>(nowMin);
  return true;
}

void SloppyClockFace::render(GfxRenderer& renderer, const Rect& viewport) {
  if (!style_ || !seeds_) return;
  unsigned hh = 0;
  unsigned mm = 0;
  standby_time::getNowHHMM(startMs_, hh, mm);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02u\n%02u", hh, mm);
  sloppy::draw(renderer, *style_, *seeds_, buf, viewport);
}

uint32_t SloppyClockFace::secondsUntilNextWake() const {
  uint32_t sec;
  if (standby_time::isSynced()) {
    const time_t now = time(nullptr);
    sec = static_cast<uint32_t>(now % 60);
  } else {
    sec = static_cast<uint32_t>(((millis() - startMs_) / 1000u) % 60u);
  }
  const uint32_t remaining = (sec == 0) ? 60u : (60u - sec);
  return remaining < 1u ? 1u : remaining;
}
