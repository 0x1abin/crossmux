#include <AutoRotation.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

void pollUntil(AutoRotation& state, float x, float y, float z, uint8_t current, uint32_t from, uint32_t to) {
  for (uint32_t delta = 0; delta < to - from; delta += 100) assert(!state.update(x, y, z, from + delta, current));
}

int main() {
  // These poses are hand-derived in the board-corrected display frame.
  struct Pose {
    float x, y, z;
    uint8_t current, want;
  };
  constexpr Pose poses[] = {{0, -1, 0, 1, 0}, {1, 0, 0, 0, 1}, {0, 1, 0, 0, 2}, {-1, 0, 0, 0, 3}};
  for (const auto p : poses) {
    AutoRotation state;
    assert(!state.update(p.x, p.y, p.z, 100, p.current));
    pollUntil(state, p.x, p.y, p.z, p.current, 200, 699);
    assert(!state.update(p.x, p.y, p.z, 699, p.current));
    assert(state.update(p.x, p.y, p.z, 700, p.current) == p.want);
    assert(!state.update(p.x, p.y, p.z, 2000, p.want));
  }
  AutoRotation sparse;
  assert(!sparse.update(1, 0, 0, 100, 0));
  assert(!sparse.update(1, 0, 0, 10100, 0));  // An unobserved interval is not stable dwell.
  pollUntil(sparse, 1, 0, 0, 0, 10200, 10700);
  assert(sparse.update(1, 0, 0, 10700, 0) == 1);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  constexpr Pose invalid[] = {
      {0, 0, 1, 0, 0}, {0.6f, 0.6f, 0, 0, 0}, {2, 0, 0, 0, 0}, {0.3f, 0, 0, 0, 0}, {0.6f, 0, 0.9f, 0, 0}};
  for (const auto p : invalid) {
    AutoRotation state;
    assert(!state.update(1, 0, 0, 0, 0));
    assert(!state.update(p.x, p.y, p.z, 600, 0));
    assert(!state.update(1, 0, 0, 700, 0));
    pollUntil(state, 1, 0, 0, 0, 800, 1299);
    assert(!state.update(1, 0, 0, 1299, 0));
    assert(state.update(1, 0, 0, 1300, 0) == 1);
  }
  for (const float bad : {nan, inf}) {
    AutoRotation state;
    assert(!state.update(bad, -1, 0, 0, 0));
    assert(!state.update(1, bad, 0, 1000, 0));
    assert(!state.update(1, 0, bad, 2000, 0));
  }
  AutoRotation jitter;
  for (uint32_t t = 0; t < 5000; t += 100) {
    assert(!jitter.update((t / 100) % 2 ? -1 : 1, 0, 0, t, 0));
  }
  AutoRotation manual;
  assert(!manual.update(1, 0, 0, 0, 0));
  assert(!manual.update(1, 0, 0, 600, 2));  // A manual orientation invalidates old dwell.
  pollUntil(manual, 1, 0, 0, 2, 700, 1200);
  assert(manual.update(1, 0, 0, 1200, 2) == 1);
  AutoRotation cooldown;
  assert(!cooldown.update(1, 0, 0, 0, 0));
  pollUntil(cooldown, 1, 0, 0, 0, 100, 600);
  assert(cooldown.update(1, 0, 0, 600, 0) == 1);
  assert(!cooldown.update(0, 1, 0, 700, 1));
  pollUntil(cooldown, 0, 1, 0, 1, 800, 1300);
  assert(!cooldown.update(0, 1, 0, 1300, 1));
  pollUntil(cooldown, 0, 1, 0, 1, 1400, 1800);
  assert(cooldown.update(0, 1, 0, 1800, 1) == 2);
  cooldown.reset();
  assert(!cooldown.update(-1, 0, 0, 2000, 2));
  pollUntil(cooldown, -1, 0, 0, 2, 2100, 2600);
  assert(cooldown.update(-1, 0, 0, 2600, 2) == 3);
  AutoRotation wrap;
  constexpr uint32_t start = 0xffffff00u;
  assert(!wrap.update(1, 0, 0, start, 0));
  pollUntil(wrap, 1, 0, 0, 0, start + uint32_t{100}, start + uint32_t{599});
  assert(!wrap.update(1, 0, 0, start + uint32_t{599}, 0));
  assert(wrap.update(1, 0, 0, start + uint32_t{600}, 0) == 1);
  assert(!wrap.update(-1, 0, 0, 1000, 99));
  std::puts("Automatic rotation four poses, dwell, invalid samples, jitter, manual change, cooldown and wrap passed");
}
