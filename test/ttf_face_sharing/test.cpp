#ifdef NDEBUG
#undef NDEBUG
#endif
#include <EpdFont.h>
#include <EpdFontData.h>
#include <EpdFontFamily.h>
#include <FontPsram.h>
#include <FtFont.h>
#define private public
#include <TtfEpdFont.h>
#undef private

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream file(argv[1], std::ios::binary);
  assert(file);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  assert(!bytes.empty());
  TtfEpdFont unloaded, rejected;
  assert(!rejected.loadVariantOf(unloaded, 10));
  TtfEpdFont master;
  master.addResidentSource(TtfEpdFont::Regular, bytes.data(), bytes.size());
  assert(master.load(12, true, 4096, 64, true));
  {
    TtfEpdFont small, large, nested;
    assert(small.loadVariantOf(master, 10, true, 4096, 64));
    assert(large.loadVariantOf(master, 20, true, 4096, 64));
    assert(nested.loadVariantOf(small, 14, true, 4096, 64));
    assert(nested.master_ == &master);
    const auto smallAdvance = small.family().getGlyph('A')->advanceX;
    const auto largeAdvance = large.family().getGlyph('A')->advanceX;
    assert(smallAdvance > 0 && largeAdvance > smallAdvance);
    assert(master.family().getData()->is4Bit && !small.family().getData()->is4Bit);
    for (uint8_t style = 0; style < 4; ++style) {
      assert(small.build("AV fi", style) && large.build("AV fi", style));
      assert(small.faces_[style].ft == master.faces_[style].ft);
      assert(large.faces_[style].ft == master.faces_[style].ft);
      if (style != TtfEpdFont::Regular) {
        TtfEpdFont::FaceLock inUse(&small, style);
        master.releaseResidentCaches();
        assert(master.faces_[style].ready);
      }
    }
    master.releaseResidentCaches();
    for (uint8_t style = 1; style < 4; ++style) assert(!master.faces_[style].ready);
    small.releaseResidentCaches();
    large.releaseResidentCaches();
    nested.releaseResidentCaches();
    for (uint8_t style = 0; style < 4; ++style) {
      assert(small.build("WAVE", style) && large.build("WAVE", style));
      assert(nested.build("WAVE", style));
    }
    assert(small.family().getGlyph('A')->advanceX == smallAdvance);
    assert(large.family().getGlyph('A')->advanceX == largeAdvance);
    small.clearCache();
    assert(small.build("A") && small.family().getGlyph('A')->advanceX == smallAdvance);
  }
  // Destroying borrowers must leave the master usable; ASan checks teardown.
  assert(master.build("AV", TtfEpdFont::Bold));
  std::cout << "Real font sharing, sizes, eviction locks, rebuild and teardown passed\n";
}
