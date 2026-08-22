#include <gtest/gtest.h>

#include <string>

#include "FileEditUtils.h"
#include "FsHelpers.h"

TEST(FileEdit, ValidatesNamesAndProtectedEntries) {
  EXPECT_TRUE(FsHelpers::isValidPathComponent("Renamed book"));
  EXPECT_FALSE(FsHelpers::isValidPathComponent(""));
  EXPECT_FALSE(FsHelpers::isValidPathComponent("."));
  EXPECT_FALSE(FsHelpers::isValidPathComponent(".."));
  EXPECT_FALSE(FsHelpers::isValidPathComponent("bad/name"));
  EXPECT_FALSE(FsHelpers::isValidPathComponent("trailing."));
  EXPECT_FALSE(FsHelpers::isValidPathComponent("trailing "));
  EXPECT_TRUE(FsHelpers::isValidPathComponent(std::string(255, 'x')));
  EXPECT_FALSE(FsHelpers::isValidPathComponent(std::string(256, 'x')));
  EXPECT_TRUE(FsHelpers::isProtectedPathComponent(".crosspoint"));
  EXPECT_TRUE(FsHelpers::isProtectedPathComponent("XTCache"));
  EXPECT_TRUE(FsHelpers::isProtectedPathComponent("System Volume Information"));
}

TEST(FileEdit, PreservesTheOriginalExtension) {
  EXPECT_EQ(FileEditUtils::withPreservedExtension("renamed", ".epub"), "renamed.epub");
  EXPECT_EQ(FileEditUtils::withPreservedExtension("notes", ".md"), "notes.md");
  EXPECT_EQ(FileEditUtils::withPreservedExtension("manual", ""), "manual");
}

TEST(FileEdit, RejectsUnsafeMoveDestinations) {
  using Error = FileEditUtils::MoveDestinationError;
  EXPECT_EQ(FileEditUtils::validateMoveDestination("/Books/a.epub", "/Books", "/Books", false, false),
            Error::SameDirectory);
  EXPECT_EQ(FileEditUtils::validateMoveDestination("/Books/Series", "/Books", "/Books/Series/Part", true, false),
            Error::OwnDescendant);
  EXPECT_EQ(FileEditUtils::validateMoveDestination("/Books/Series", "/Books", "/Books/Series", true, false),
            Error::OwnDescendant);
  EXPECT_EQ(FileEditUtils::validateMoveDestination("/Books/a.epub", "/Books", "/Archive", false, true),
            Error::TargetExists);
  EXPECT_EQ(FileEditUtils::validateMoveDestination("/Books/a.epub", "/Books", "/Archive", false, false), Error::None);
}

TEST(FileEdit, RewritesOnlyPathSegmentPrefixes) {
  EXPECT_TRUE(FsHelpers::isSameOrDescendantPath("/Books", "/"));
  EXPECT_TRUE(FsHelpers::isSameOrDescendantPath("/Books/Series/a.epub", "/Books/Series"));
  EXPECT_FALSE(FsHelpers::isSameOrDescendantPath("/Books/Series 2/a.epub", "/Books/Series"));
  EXPECT_EQ(FsHelpers::rebasePath("/Books/Series/a.epub", "/Books/Series", "/Archive/Series"),
            "/Archive/Series/a.epub");
  EXPECT_EQ(FsHelpers::rebasePath("/Books/Series", "/Books/Series", "/Archive/Series"), "/Archive/Series");
  EXPECT_EQ(FsHelpers::rebasePath("/Books/Series 2/a.epub", "/Books/Series", "/Archive/Series"),
            "/Books/Series 2/a.epub");
}
