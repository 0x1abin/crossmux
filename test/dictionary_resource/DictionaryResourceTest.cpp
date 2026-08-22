#include <gtest/gtest.h>

#include "DictionaryResource.h"

using DictionaryResource::LocalState;

TEST(DictionaryResource, RejectsUnsafeIdsAndFileNames) {
  EXPECT_TRUE(DictionaryResource::isValidId("wikdict-en_zh"));
  EXPECT_FALSE(DictionaryResource::isValidId(".hidden"));
  EXPECT_FALSE(DictionaryResource::isValidId("../escape"));
  EXPECT_FALSE(DictionaryResource::isValidId(std::string(32, 'a')));
  EXPECT_TRUE(DictionaryResource::isValidFileName("dict", "dict.dict.dz"));
  EXPECT_FALSE(DictionaryResource::isValidFileName("dict", "dict.idx.gz"));
  EXPECT_FALSE(DictionaryResource::isValidFileName("dict", "other.idx"));
}

TEST(DictionaryResource, ParsesOnlyCanonicalOwnershipMarkers) {
  EXPECT_EQ(DictionaryResource::parseMarkerRevision("1:3\n"), 3);
  EXPECT_EQ(DictionaryResource::parseMarkerRevision("1:0\n"), -1);
  EXPECT_EQ(DictionaryResource::parseMarkerRevision("2:3\n"), -1);
  EXPECT_EQ(DictionaryResource::parseMarkerRevision("1:3"), -1);
}

TEST(DictionaryResource, ProtectsManualDictionariesAndFindsUpdates) {
  EXPECT_EQ(DictionaryResource::classify(false, false, -1, 3), LocalState::NotInstalled);
  EXPECT_EQ(DictionaryResource::classify(false, true, 3, 3), LocalState::Installed);
  EXPECT_EQ(DictionaryResource::classify(false, true, 2, 3), LocalState::UpdateAvailable);
  EXPECT_EQ(DictionaryResource::classify(true, false, -1, 3), LocalState::Conflict);
  EXPECT_EQ(DictionaryResource::classify(false, true, -1, 3), LocalState::Conflict);
}
