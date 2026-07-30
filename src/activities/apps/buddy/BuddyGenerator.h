#pragma once

#include <HalSystem.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace buddy {

enum class Rarity : uint8_t { Common, Uncommon, Rare, Epic, Legendary, Count };
enum class Species : uint8_t {
  Duck,
  Goose,
  Blob,
  Cat,
  Dragon,
  Octopus,
  Owl,
  Penguin,
  Turtle,
  Snail,
  Ghost,
  Axolotl,
  Capybara,
  Cactus,
  Robot,
  Rabbit,
  Mushroom,
  Chonk,
  Count
};
enum class Eye : uint8_t { Dot, Sparkle, Cross, Bullseye, At, Ring, Count };
enum class Hat : uint8_t { None, Crown, TopHat, Propeller, Halo, Wizard, Beanie, TinyDuck, Count };
enum class Stat : uint8_t { Focus, Patience, Luck, Wisdom, Courage, Count };

struct Traits {
  Rarity rarity = Rarity::Common;
  Species species = Species::Duck;
  Eye eye = Eye::Dot;
  Hat hat = Hat::None;
  bool shiny = false;
  std::array<uint8_t, static_cast<size_t>(Stat::Count)> stats{};
  std::array<char, 13> name{};
};

Traits generate(const HalSystem::DeviceId& deviceId);

}  // namespace buddy
