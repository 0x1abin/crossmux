#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "ExFatLib/ExFatPartition.h"
#include "FatLib/FatPartition.h"

// Synthetic sectors only: no filesystem writes and no physical card access.
class TestCard final : public FsBlockDeviceInterface {
 public:
  static constexpr uint32_t clusters = 100000;
  static constexpr uint32_t fatSectors = (clusters + 2 + 127) / 128;
  bool exfat = false;
  bool failReads = false;
  uint8_t clusterShift = 7;
  unsigned reads = 0;
  bool isBusy() override { return false; }
  uint32_t sectorCount() override { return 32 + 2 * fatSectors + (clusters << clusterShift); }
  bool syncDevice() override { return true; }
  bool writeSector(uint32_t, const uint8_t*) override { return false; }
  bool writeSectors(uint32_t, const uint8_t*, size_t) override { return false; }
  bool readSector(uint32_t sector, uint8_t* dst) override {
    ++reads;
    if (failReads) return false;
    memset(dst, 0, 512);
    if (sector == 0) {
      if (exfat) {
        memcpy(dst + 3, "EXFAT   ", 8);
        setLe64(dst + 72, sectorCount());
        setLe32(dst + 80, 32);
        setLe32(dst + 84, fatSectors);
        setLe32(dst + 88, 32 + 2 * fatSectors);
        setLe32(dst + 92, clusters);
        setLe32(dst + 96, 3);
        dst[108] = 9;
        dst[109] = clusterShift;
        dst[110] = 1;
      } else {
        setLe16(dst + 11, 512);
        dst[13] = 1U << clusterShift;
        setLe16(dst + 14, 32);
        dst[16] = 2;
        setLe32(dst + 32, sectorCount());
        setLe32(dst + 36, fatSectors);
        setLe32(dst + 44, 2);
      }
      dst[510] = 0x55;
      dst[511] = 0xaa;
    } else if (!exfat && sector == 32) {
      setLe32(dst, 0x0ffffff8);
      setLe32(dst + 4, 0x0fffffff);
      setLe32(dst + 8, 0x0fffffff);  // Root directory consumes one cluster.
    } else if (exfat && sector == 32 + 2 * fatSectors) {
      dst[0] = 3;  // Bitmap and root directory consume two clusters.
    }
    return true;
  }
  bool readSectors(uint32_t sector, uint8_t* dst, size_t count) override {
    for (size_t i = 0; i < count; ++i) {
      if (!readSector(sector + i, dst + 512 * i)) return false;
    }
    return true;
  }
};

uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }

// Only volume dispatch, clock, and mount lifecycle are doubles. Partition scans
// use the installed SdFat implementation; overrides inject otherwise rare errors.
class FsVolume {
 public:
  FatPartition* fat = nullptr;
  ExFatPartition* exfat = nullptr;
  int32_t freeOverride = -2;
  uint32_t sectorOverride = UINT32_MAX;
  uint32_t clusterOverride = UINT32_MAX;
  uint32_t scanDurationMs = 0;
  unsigned scans = 0;
  uint32_t clusterCount() {
    if (clusterOverride != UINT32_MAX) return clusterOverride;
    return fat ? fat->clusterCount() : exfat->clusterCount();
  }
  uint32_t sectorsPerCluster() {
    if (sectorOverride != UINT32_MAX) return sectorOverride;
    return fat ? fat->sectorsPerCluster() : exfat->sectorsPerCluster();
  }
  int32_t freeClusterCount() {
    ++scans;
    clockMs += scanDurationMs;
    if (freeOverride != -2) return freeOverride;
    return fat ? fat->freeClusterCount() : exfat->freeClusterCount();
  }
};

uint64_t volumeTotalBytes(FsVolume& volume);
class SDCardManager {
 public:
  explicit SDCardManager(FsVolume& volume) : volume(volume) {}
  bool getSpace(uint64_t& totalBytes, uint64_t& freeBytes);
  uint64_t sdTotalBytes() const;
  uint64_t sdUsedBytes();
  void mount(bool ready = true) {
    initialized = ready;
    cachedTotalBytes = ready ? volumeTotalBytes(volume) : 0;
    cachedUsedBytesValid = false;
  }

 private:
  FsVolume& volume;
  FsVolume& vol() { return volume; }
  bool initialized = false;
#include "cache.inc"
};

#include "methods.inc"

void expectFailure(SDCardManager& manager) {
  uint64_t total = 123, free = 456;
  assert(!manager.getSpace(total, free));
  assert(total == 0 && free == 0);
}

int main() {
  for (bool exfat : {false, true}) {
    for (uint8_t shift : {uint8_t(6), uint8_t(7)}) {
      TestCard card;
      card.exfat = exfat;
      card.clusterShift = shift;
      FatPartition fat;
      ExFatPartition ex;
      FsVolume volume;
      if (exfat) {
        assert(ex.init(&card, 0));
        volume.exfat = &ex;
      } else {
        assert(fat.init(&card, 0));
        assert(fat.fatType() == 32);
        volume.fat = &fat;
      }
      SDCardManager manager(volume);
      expectFailure(manager);
      manager.mount();
      uint64_t total, free;
      const uint64_t clusterBytes = 512ULL << shift;
      const uint64_t expectedUsed = (exfat ? 2 : 1) * clusterBytes;
      assert(manager.getSpace(total, free));
      assert(total == TestCard::clusters * clusterBytes);
      assert(total - free == expectedUsed);
      assert(manager.sdTotalBytes() == total && manager.sdUsedBytes() == expectedUsed);
      assert(volume.scans == 1);
      printf("%s cluster=%llu: total=%llu used=%llu OK\n", exfat ? "exFAT" : "FAT32", (unsigned long long)clusterBytes,
             (unsigned long long)total, (unsigned long long)(total - free));

      // Failed refresh discards the previous success and retries immediately.
      clockMs += 20000;
      card.failReads = true;
      expectFailure(manager);
      assert(manager.sdUsedBytes() == 0);
      card.failReads = false;
      assert(manager.getSpace(total, free));
      assert(total - free == expectedUsed);
      manager.mount(false);
      expectFailure(manager);
      assert(manager.sdTotalBytes() == 0);

      // Empty/full volumes and corrupt free-cluster counts.
      for (int32_t count : {int32_t(TestCard::clusters), 0, -1, int32_t(TestCard::clusters + 1)}) {
        manager.mount();
        volume.freeOverride = count;
        if (count < 0 || count > int32_t(TestCard::clusters)) {
          expectFailure(manager);
          volume.freeOverride = 0;
          assert(manager.getSpace(total, free) && free == 0);
        } else {
          assert(manager.getSpace(total, free));
          assert(free == uint64_t(count) * clusterBytes);
        }
      }
      for (uint32_t sectors : {0U, 3U}) {
        volume.sectorOverride = sectors;
        manager.mount();
        expectFailure(manager);
      }
      volume.sectorOverride = UINT32_MAX;
      volume.clusterOverride = 0;
      manager.mount();
      expectFailure(manager);
      volume.clusterOverride = UINT32_MAX;

      // Cache age starts after a slow scan; unsigned subtraction survives millis wrap.
      manager.mount();
      clockMs = UINT32_MAX - 1000;
      volume.scanDurationMs = 4800;
      assert(manager.getSpace(total, free));
      const auto scans = volume.scans;
      clockMs += 19999;
      assert(manager.getSpace(total, free) && volume.scans == scans);
      ++clockMs;
      assert(manager.getSpace(total, free) && volume.scans == scans + 1);
    }
  }
  puts("SD capacity, scan failure, geometry, cache expiry/retry and wrap checks passed.");
}
