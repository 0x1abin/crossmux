#include <gtest/gtest.h>

#include <cstring>

#include "CalculatorState.h"

namespace {

using calculator::CalculatorState;
using calculator::Key;

Key digitKey(const char digit) {
  return static_cast<Key>(static_cast<uint8_t>(Key::Digit0) + static_cast<uint8_t>(digit - '0'));
}

void enter(CalculatorState& state, const char* text) {
  for (; *text != '\0'; ++text) state.press(*text == '.' ? Key::Decimal : digitKey(*text));
}

TEST(CalculatorState, MultipliesDecimal) {
  CalculatorState state;
  enter(state, "12.5");
  state.press(Key::Multiply);
  enter(state, "8");
  state.press(Key::Equals);
  EXPECT_STREQ(state.resultText(), "100");
}

TEST(CalculatorState, EvaluatesLeftToRight) {
  CalculatorState state;
  enter(state, "2");
  state.press(Key::Add);
  enter(state, "3");
  state.press(Key::Multiply);
  enter(state, "4");
  state.press(Key::Equals);
  EXPECT_STREQ(state.resultText(), "20");
}

TEST(CalculatorState, AppliesRelativePercent) {
  CalculatorState state;
  enter(state, "200");
  state.press(Key::Add);
  enter(state, "10");
  state.press(Key::Percent);
  state.press(Key::Equals);
  EXPECT_STREQ(state.resultText(), "220");
}

TEST(CalculatorState, TogglesSignBackspacesAndClears) {
  CalculatorState state;
  enter(state, "123");
  state.press(Key::ToggleSign);
  EXPECT_STREQ(state.resultText(), "-123");
  state.press(Key::Backspace);
  EXPECT_STREQ(state.resultText(), "-12");
  state.press(Key::ToggleSign);
  EXPECT_STREQ(state.resultText(), "12");
  state.press(Key::Clear);
  EXPECT_STREQ(state.resultText(), "0");
  EXPECT_STREQ(state.expressionText(), "");
}

TEST(CalculatorState, ReplacesPendingOperator) {
  CalculatorState state;
  enter(state, "9");
  state.press(Key::Add);
  state.press(Key::Subtract);
  enter(state, "4");
  state.press(Key::Equals);
  EXPECT_STREQ(state.resultText(), "5");
}

TEST(CalculatorState, RecoversFromDivisionByZeroWithDigit) {
  CalculatorState state;
  enter(state, "8");
  state.press(Key::Divide);
  enter(state, "0");
  state.press(Key::Equals);
  EXPECT_TRUE(state.hasError());

  enter(state, "7");
  EXPECT_FALSE(state.hasError());
  EXPECT_STREQ(state.resultText(), "7");
}

TEST(CalculatorState, RepeatedEqualsDoesNotRepeatOperation) {
  CalculatorState state;
  enter(state, "2");
  state.press(Key::Add);
  enter(state, "3");
  state.press(Key::Equals);
  state.press(Key::Equals);
  EXPECT_STREQ(state.resultText(), "5");
}

TEST(CalculatorState, LimitsInputAndFormatsToTwelveSignificantDigits) {
  CalculatorState state;
  enter(state, "1234567890123");
  EXPECT_STREQ(state.resultText(), "123456789012");

  state.press(Key::Clear);
  enter(state, "1");
  state.press(Key::Divide);
  enter(state, "3");
  state.press(Key::Equals);
  EXPECT_STREQ(state.resultText(), "0.333333333333");
  EXPECT_LE(std::strlen(state.resultText()), 31U);
}

}  // namespace
