#pragma once
#include "BleInput.h"
class MappedInputManager;
namespace bleharness {
void reset(GfxRenderer& renderer, MappedInputManager* input = nullptr);
bool frame(MappedInputManager& input);
}  // namespace bleharness
