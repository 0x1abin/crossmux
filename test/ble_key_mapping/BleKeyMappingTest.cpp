#include <gtest/gtest.h>

#include "BleKeyMapping.h"

using bleinput::Action;
using bleinput::KeyMap;

TEST(BleKeyMapping, AssignRebindAndLookup) {
  KeyMap map{};
  EXPECT_TRUE(bleinput::assign(map, 0, 12, Action::PageForward));
  EXPECT_TRUE(bleinput::assign(map, 1, 44, Action::PageForward));
  EXPECT_TRUE(bleinput::assign(map, 1, 44, Action::Back));

  Action action = Action::PageForward;
  EXPECT_FALSE(bleinput::lookup(map, 0, 12, action));
  ASSERT_TRUE(bleinput::lookup(map, 1, 44, action));
  EXPECT_EQ(action, Action::Back);
  EXPECT_EQ(bleinput::findByAction(map, Action::PageForward), nullptr);
}

TEST(BleKeyMapping, CapacityAndPersistedValidation) {
  KeyMap map{};
  for (uint8_t i = 0; i < static_cast<uint8_t>(Action::Count); ++i) {
    ASSERT_TRUE(bleinput::appendValidated(map, i & 1, i + 1, i));
  }
  EXPECT_FALSE(bleinput::appendValidated(map, 0, 42, static_cast<uint8_t>(Action::Right)));

  KeyMap loaded{};
  EXPECT_FALSE(bleinput::appendValidated(loaded, 2, 1, 0));
  EXPECT_FALSE(bleinput::appendValidated(loaded, 0, 0, 0));
  EXPECT_FALSE(bleinput::appendValidated(loaded, 0, 1, 8));
  EXPECT_TRUE(bleinput::appendValidated(loaded, 0, 1, 0));
  EXPECT_FALSE(bleinput::appendValidated(loaded, 0, 1, 1));
  EXPECT_FALSE(bleinput::appendValidated(loaded, 1, 2, 0));

  KeyMap full{};
  for (uint8_t i = 0; i < full.size(); ++i) full[i] = {2, static_cast<uint8_t>(i + 1), 0xFE};
  EXPECT_FALSE(bleinput::appendValidated(full, 0, 42, static_cast<uint8_t>(Action::PageForward)));
}
