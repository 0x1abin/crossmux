#include "BuddyGenerator.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace buddy {
namespace {

constexpr uint64_t kWySecret[] = {
    0xa0761d6478bd642fULL,
    0xe7037ed1a0b428dbULL,
    0x8ebc6af09c88c6e3ULL,
    0x589965cc75374cc3ULL,
};
constexpr char kSalt[] = "friend-2026-401";
constexpr uint32_t kShinyThreshold = 42949673u;  // ceil(2^32 * 0.01)
constexpr uint32_t kNameSeedMask = 0x0BADD1E5u;

constexpr const char* kNamePrefixes[] = {
    "Nib",  "Pip",  "Crum", "Sprig", "Tink", "Moss",   "Quib", "Wob",
    "Dusk", "Fizz", "Plop", "Brim",  "Nox",  "Sprock", "Puck", "Mallow",
};
constexpr const char* kNameSuffixes[] = {
    "let", "bit",   "bun",    "wick", "puff",  "pod",    "kin",   "mote",
    "zip", "snoot", "sprout", "fuzz", "blink", "pebble", "crumb", "bop",
};

uint64_t readLittleEndian(const uint8_t* data, const size_t bytes) {
  uint64_t value = 0;
  for (size_t i = 0; i < bytes; ++i) value |= static_cast<uint64_t>(data[i]) << (i * 8);
  return value;
}

void multiply128(const uint64_t lhs, const uint64_t rhs, uint64_t& low, uint64_t& high) {
  const uint64_t lhsLow = static_cast<uint32_t>(lhs);
  const uint64_t lhsHigh = lhs >> 32;
  const uint64_t rhsLow = static_cast<uint32_t>(rhs);
  const uint64_t rhsHigh = rhs >> 32;
  const uint64_t p00 = lhsLow * rhsLow;
  const uint64_t p01 = lhsLow * rhsHigh;
  const uint64_t p10 = lhsHigh * rhsLow;
  const uint64_t p11 = lhsHigh * rhsHigh;

  low = p00;
  uint64_t carry = 0;
  const uint64_t add01 = p01 << 32;
  const uint64_t next01 = low + add01;
  carry += next01 < low;
  low = next01;
  const uint64_t add10 = p10 << 32;
  const uint64_t next10 = low + add10;
  carry += next10 < low;
  low = next10;
  high = p11 + (p01 >> 32) + (p10 >> 32) + carry;
}

uint64_t mix(uint64_t lhs, uint64_t rhs) {
  uint64_t high = 0;
  multiply128(lhs, rhs, lhs, high);
  return lhs ^ high;
}

class Mulberry32 {
 public:
  explicit Mulberry32(const uint32_t seed) : state_(seed) {}

  uint32_t next() {
    state_ += 0x6D2B79F5u;
    uint32_t value = state_;
    value = static_cast<uint32_t>(static_cast<uint64_t>(value ^ (value >> 15)) * (1u | value));
    value ^=
        value + static_cast<uint32_t>(static_cast<uint64_t>(value ^ (value >> 7)) * static_cast<uint32_t>(61u | value));
    return value ^ (value >> 14);
  }

  uint32_t index(const uint32_t count) { return static_cast<uint32_t>((static_cast<uint64_t>(next()) * count) >> 32); }

 private:
  uint32_t state_;
};

Rarity rollRarity(Mulberry32& rng) {
  const uint32_t roll = rng.index(100);
  if (roll < 60) return Rarity::Common;
  if (roll < 85) return Rarity::Uncommon;
  if (roll < 95) return Rarity::Rare;
  if (roll < 99) return Rarity::Epic;
  return Rarity::Legendary;
}

uint8_t rarityFloor(const Rarity rarity) {
  constexpr uint8_t floors[] = {5, 15, 25, 35, 50};
  return floors[static_cast<size_t>(rarity)];
}

void generateName(const uint32_t seed, Traits& traits) {
  Mulberry32 rng(seed ^ kNameSeedMask);
  const char* prefix = kNamePrefixes[rng.index(std::size(kNamePrefixes))];
  const char* suffix = kNameSuffixes[rng.index(std::size(kNameSuffixes))];
  snprintf(traits.name.data(), traits.name.size(), "%s%s", prefix, suffix);
}

uint64_t wyhash(const uint8_t* data, const size_t length, const uint64_t seed) {
  uint64_t state = seed ^ mix(seed ^ kWySecret[0], kWySecret[1]);
  uint64_t a = 0;
  uint64_t b = 0;

  if (length <= 16) {
    if (length >= 4) {
      const size_t end = length - 4;
      const size_t quarter = (length >> 3) << 2;
      a = (readLittleEndian(data, 4) << 32) | readLittleEndian(data + quarter, 4);
      b = (readLittleEndian(data + end, 4) << 32) | readLittleEndian(data + end - quarter, 4);
    } else if (length > 0) {
      a = (static_cast<uint64_t>(data[0]) << 16) | (static_cast<uint64_t>(data[length >> 1]) << 8) | data[length - 1];
    }
  } else {
    size_t offset = 0;
    if (length >= 48) {
      uint64_t state1 = state;
      uint64_t state2 = state;
      while (offset + 48 < length) {
        state = mix(readLittleEndian(data + offset, 8) ^ kWySecret[1], readLittleEndian(data + offset + 8, 8) ^ state);
        state1 = mix(readLittleEndian(data + offset + 16, 8) ^ kWySecret[2],
                     readLittleEndian(data + offset + 24, 8) ^ state1);
        state2 = mix(readLittleEndian(data + offset + 32, 8) ^ kWySecret[3],
                     readLittleEndian(data + offset + 40, 8) ^ state2);
        offset += 48;
      }
      state ^= state1 ^ state2;
    }
    while (offset + 16 < length) {
      state = mix(readLittleEndian(data + offset, 8) ^ kWySecret[1], readLittleEndian(data + offset + 8, 8) ^ state);
      offset += 16;
    }
    a = readLittleEndian(data + length - 16, 8);
    b = readLittleEndian(data + length - 8, 8);
  }

  a ^= kWySecret[1];
  b ^= state;
  uint64_t high = 0;
  multiply128(a, b, a, high);
  b = high;
  return mix(a ^ kWySecret[0] ^ length, b ^ kWySecret[1]);
}

Traits generateTraits(const uint32_t seed) {
  Mulberry32 rng(seed);
  Traits traits;
  traits.rarity = rollRarity(rng);
  traits.species = static_cast<Species>(rng.index(static_cast<uint32_t>(Species::Count)));
  traits.eye = static_cast<Eye>(rng.index(static_cast<uint32_t>(Eye::Count)));
  if (traits.rarity != Rarity::Common) {
    traits.hat = static_cast<Hat>(rng.index(static_cast<uint32_t>(Hat::Count)));
  }
  traits.shiny = rng.next() < kShinyThreshold;

  const size_t statCount = static_cast<size_t>(Stat::Count);
  const size_t peak = rng.index(static_cast<uint32_t>(statCount));
  size_t dump = peak;
  while (dump == peak) dump = rng.index(static_cast<uint32_t>(statCount));

  const uint8_t floor = rarityFloor(traits.rarity);
  for (size_t i = 0; i < statCount; ++i) {
    if (i == peak) {
      traits.stats[i] = static_cast<uint8_t>(std::min<uint32_t>(100, floor + 50U + rng.index(30)));
    } else if (i == dump) {
      const int dumpValue = static_cast<int>(floor) - 10 + static_cast<int>(rng.index(15));
      traits.stats[i] = static_cast<uint8_t>(std::max(1, dumpValue));
    } else {
      traits.stats[i] = static_cast<uint8_t>(floor + rng.index(40));
    }
  }
  generateName(seed, traits);
  return traits;
}

}  // namespace

Traits generate(const HalSystem::DeviceId& deviceId) {
  char accountUuid[13];
  snprintf(accountUuid, sizeof(accountUuid), "%02x%02x%02x%02x%02x%02x", deviceId[0], deviceId[1], deviceId[2],
           deviceId[3], deviceId[4], deviceId[5]);

  std::array<uint8_t, 27> salted{};
  memcpy(salted.data(), accountUuid, sizeof(accountUuid) - 1);
  memcpy(salted.data() + sizeof(accountUuid) - 1, kSalt, sizeof(kSalt) - 1);
  const uint32_t seed = static_cast<uint32_t>(wyhash(salted.data(), salted.size(), 0));
  return generateTraits(seed);
}

}  // namespace buddy
