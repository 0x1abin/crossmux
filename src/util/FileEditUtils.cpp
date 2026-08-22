#include "FileEditUtils.h"

#include <FsHelpers.h>

namespace FileEditUtils {

std::string withPreservedExtension(const std::string_view newStem, const std::string_view extension) {
  std::string result(newStem);
  result.append(extension);
  return result;
}

MoveDestinationError validateMoveDestination(const std::string_view sourcePath, const std::string_view sourceDirectory,
                                             const std::string_view destinationDirectory, const bool sourceIsDirectory,
                                             const bool targetExists) {
  if (destinationDirectory == sourceDirectory) return MoveDestinationError::SameDirectory;
  if (sourceIsDirectory && FsHelpers::isSameOrDescendantPath(destinationDirectory, sourcePath)) {
    return MoveDestinationError::OwnDescendant;
  }
  if (targetExists) return MoveDestinationError::TargetExists;
  return MoveDestinationError::None;
}

}  // namespace FileEditUtils
