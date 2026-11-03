#include <HalOtaSlot.h>
#include <HalStorage.h>
#include <SdCardFontCache.h>

#include <cassert>

int main() {
  using namespace font_cache_test;
  using namespace SdCardFontCache;
  constexpr auto path = "/fonts/Serif/Serif_16.cpfont";
  const auto original = flash;
  const auto rejected = [&](Result expected) {
    assert(preflight(path) == expected);
    assert(preload(path) == expected);
    assert(writes == 0 && erases == 0 && flash == original);
  };

  // Eligibility is read-only, including exact-capacity fonts.
  assert(capacity() == 4096);
  for (size_t size : {4095u, 4096u}) {
    sourceSize = size;
    assert(preflight(path) == Result::Ok);
    assert(writes == 0 && erases == 0 && flash == original);
  }
  sourceSize = 4097;
  rejected(Result::TooLarge);

  // Respect both the actual partition size and the global payload ceiling.
  slotSize = 8 * 1024 * 1024;
  assert(capacity() == 6549504);
  sourceSize = capacity();
  assert(preflight(path) == Result::Ok);
  ++sourceSize;
  rejected(Result::TooLarge);
  slotSize = flash.size();
  sourceSize = 64;

  slotValid = false;
  rejected(Result::NotSafe);
  slotValid = true;
  slotSafe = false;
  rejected(Result::NotSafe);
  slotSafe = true;
  sourceExists = false;
  rejected(Result::InvalidFont);
  sourceExists = true;
  sourceHeader[8] = 0;
  rejected(Result::InvalidFont);
  sourceHeader[8] = 4;
  sourceSize = 63;
  rejected(Result::InvalidFont);
  sourceSize = 64;

  // Recheck at execution: the source/slot can change while the prompt is open.
  assert(preflight(path) == Result::Ok);
  sourceSize = 4097;
  assert(preload(path) == Result::TooLarge);
  sourceSize = 64;
  assert(preflight(path) == Result::Ok);
  sourceExists = false;
  assert(preload(path) == Result::InvalidFont);
  sourceExists = true;
  assert(preflight(path) == Result::Ok);
  slotSafe = false;
  assert(preload(path) == Result::NotSafe);
  slotSafe = true;
  assert(writes == 0 && erases == 0 && flash == original);

  assert(preload(path) == Result::Ok);
  assert(writes > 0 && erases > 0);
  const auto committed = flash;
  writes = erases = 0;
  assert(preflight(path) == Result::AlreadyCached);
  assert(preload(path) == Result::AlreadyCached);
  assert(writes == 0 && erases == 0 && flash == committed);

  // Rejecting a replacement must also leave an existing cache intact.
  sourceSize = 4097;
  assert(preflight(path) == Result::TooLarge);
  assert(preload(path) == Result::TooLarge);
  assert(writes == 0 && erases == 0 && flash == committed);
}
