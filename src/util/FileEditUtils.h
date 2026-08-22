#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace FileEditUtils {

enum class MoveDestinationError : uint8_t { None, SameDirectory, OwnDescendant, TargetExists };

std::string withPreservedExtension(std::string_view newStem, std::string_view extension);

MoveDestinationError validateMoveDestination(std::string_view sourcePath, std::string_view sourceDirectory,
                                             std::string_view destinationDirectory, bool sourceIsDirectory,
                                             bool targetExists);

}  // namespace FileEditUtils
