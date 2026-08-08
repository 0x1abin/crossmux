#pragma once

#include <cstddef>
#include <cstdint>

namespace calculator {

enum class Key : uint8_t {
  Digit0,
  Digit1,
  Digit2,
  Digit3,
  Digit4,
  Digit5,
  Digit6,
  Digit7,
  Digit8,
  Digit9,
  Decimal,
  Add,
  Subtract,
  Multiply,
  Divide,
  Percent,
  ToggleSign,
  Backspace,
  Clear,
  Equals,
  Count,
};

enum class Operator : uint8_t { None, Add, Subtract, Multiply, Divide };

class CalculatorState {
 public:
  static constexpr int MAX_INPUT_DIGITS = 12;

  CalculatorState();

  void press(Key key);
  void reset();

  const char* resultText() const { return result_; }
  const char* expressionText() const { return expression_; }
  bool hasError() const { return error_; }

 private:
  static constexpr size_t NUMBER_CAPACITY = 32;
  static constexpr size_t EXPRESSION_CAPACITY = 80;

  void pressDigit(char digit);
  void pressDecimal();
  void pressOperator(Operator op);
  void pressEquals();
  void pressPercent();
  void toggleSign();
  void backspace();

  bool applyPending(double right);
  bool setInputFromValue(double value);
  void updateExpression(bool withEquals = false);
  void updateResultFromInput();
  void setError();
  double inputValue() const;

  static int significantDigitCount(const char* text);
  static const char* operatorText(Operator op);
  static bool formatNumber(double value, char* output, size_t outputSize);

  double accumulator_ = 0.0;
  Operator pending_ = Operator::None;
  bool entering_ = false;
  bool justEvaluated_ = false;
  bool percentApplied_ = false;
  bool error_ = false;
  char input_[NUMBER_CAPACITY]{};
  char left_[NUMBER_CAPACITY]{};
  char percentLabel_[NUMBER_CAPACITY]{};
  char expression_[EXPRESSION_CAPACITY]{};
  char result_[NUMBER_CAPACITY]{};
};

}  // namespace calculator
