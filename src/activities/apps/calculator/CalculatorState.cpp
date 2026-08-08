#include "CalculatorState.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace calculator {

CalculatorState::CalculatorState() { reset(); }

void CalculatorState::reset() {
  accumulator_ = 0.0;
  pending_ = Operator::None;
  entering_ = false;
  justEvaluated_ = false;
  percentApplied_ = false;
  error_ = false;
  std::strcpy(input_, "0");
  left_[0] = '\0';
  percentLabel_[0] = '\0';
  expression_[0] = '\0';
  std::strcpy(result_, "0");
}

void CalculatorState::press(const Key key) {
  switch (key) {
    case Key::Digit0:
    case Key::Digit1:
    case Key::Digit2:
    case Key::Digit3:
    case Key::Digit4:
    case Key::Digit5:
    case Key::Digit6:
    case Key::Digit7:
    case Key::Digit8:
    case Key::Digit9:
      pressDigit(static_cast<char>('0' + static_cast<uint8_t>(key) - static_cast<uint8_t>(Key::Digit0)));
      break;
    case Key::Decimal:
      pressDecimal();
      break;
    case Key::Add:
      pressOperator(Operator::Add);
      break;
    case Key::Subtract:
      pressOperator(Operator::Subtract);
      break;
    case Key::Multiply:
      pressOperator(Operator::Multiply);
      break;
    case Key::Divide:
      pressOperator(Operator::Divide);
      break;
    case Key::Percent:
      pressPercent();
      break;
    case Key::ToggleSign:
      toggleSign();
      break;
    case Key::Backspace:
      backspace();
      break;
    case Key::Clear:
      reset();
      break;
    case Key::Equals:
      pressEquals();
      break;
    case Key::Count:
      break;
  }
}

void CalculatorState::pressDigit(const char digit) {
  if (error_ || justEvaluated_) reset();
  if (percentApplied_) {
    entering_ = false;
    percentApplied_ = false;
  }

  if (!entering_) {
    input_[0] = digit;
    input_[1] = '\0';
    entering_ = true;
  } else if (std::strcmp(input_, "0") == 0) {
    input_[0] = digit;
  } else if (std::strcmp(input_, "-0") == 0) {
    input_[1] = digit;
  } else {
    const size_t length = std::strlen(input_);
    if (significantDigitCount(input_) >= MAX_INPUT_DIGITS || length + 1 >= NUMBER_CAPACITY) return;
    input_[length] = digit;
    input_[length + 1] = '\0';
  }

  updateResultFromInput();
  updateExpression();
}

void CalculatorState::pressDecimal() {
  if (error_ || justEvaluated_) reset();
  if (percentApplied_) {
    entering_ = false;
    percentApplied_ = false;
  }

  if (!entering_) {
    std::strcpy(input_, "0.");
    entering_ = true;
  } else if (std::strchr(input_, '.') == nullptr) {
    const size_t length = std::strlen(input_);
    if (length + 1 >= NUMBER_CAPACITY) return;
    input_[length] = '.';
    input_[length + 1] = '\0';
  }

  updateResultFromInput();
  updateExpression();
}

void CalculatorState::pressOperator(const Operator op) {
  if (error_) return;
  justEvaluated_ = false;

  if (pending_ != Operator::None) {
    if (!entering_) {
      pending_ = op;
      updateExpression();
      return;
    }
    if (!applyPending(inputValue())) return;
  } else {
    accumulator_ = inputValue();
  }

  if (!formatNumber(accumulator_, left_, sizeof(left_))) {
    setError();
    return;
  }
  pending_ = op;
  entering_ = false;
  percentApplied_ = false;
  updateExpression();
}

void CalculatorState::pressEquals() {
  if (error_ || pending_ == Operator::None || !entering_) return;

  updateExpression(true);
  if (!applyPending(inputValue())) return;
  pending_ = Operator::None;
  entering_ = false;
  justEvaluated_ = true;
  percentApplied_ = false;
}

void CalculatorState::pressPercent() {
  if (error_ || (pending_ != Operator::None && !entering_)) return;
  justEvaluated_ = false;
  entering_ = true;

  std::snprintf(percentLabel_, sizeof(percentLabel_), "%s%%", input_);
  const double raw = inputValue();
  const bool relative = pending_ == Operator::Add || pending_ == Operator::Subtract;
  const double value = relative ? accumulator_ * raw / 100.0 : raw / 100.0;
  if (!setInputFromValue(value)) return;
  percentApplied_ = true;
  updateExpression();
}

void CalculatorState::toggleSign() {
  if (error_) return;
  if (justEvaluated_) {
    justEvaluated_ = false;
    entering_ = true;
  }
  if (!entering_) {
    std::strcpy(input_, "0");
    entering_ = true;
  }
  percentApplied_ = false;

  if (input_[0] == '-') {
    std::memmove(input_, input_ + 1, std::strlen(input_));
  } else {
    const size_t length = std::strlen(input_);
    if (length + 1 >= NUMBER_CAPACITY) return;
    std::memmove(input_ + 1, input_, length + 1);
    input_[0] = '-';
  }
  updateResultFromInput();
  updateExpression();
}

void CalculatorState::backspace() {
  if (error_ || justEvaluated_) {
    reset();
    return;
  }
  if (!entering_) return;

  percentApplied_ = false;
  const size_t length = std::strlen(input_);
  if (length <= 1 || (length == 2 && input_[0] == '-')) {
    std::strcpy(input_, "0");
  } else {
    input_[length - 1] = '\0';
  }
  updateResultFromInput();
  updateExpression();
}

bool CalculatorState::applyPending(const double right) {
  double value = accumulator_;
  switch (pending_) {
    case Operator::None:
      value = right;
      break;
    case Operator::Add:
      value += right;
      break;
    case Operator::Subtract:
      value -= right;
      break;
    case Operator::Multiply:
      value *= right;
      break;
    case Operator::Divide:
      if (right == 0.0) {
        setError();
        return false;
      }
      value /= right;
      break;
  }

  if (!setInputFromValue(value)) return false;
  accumulator_ = value;
  return true;
}

bool CalculatorState::setInputFromValue(const double value) {
  if (!formatNumber(value, input_, sizeof(input_))) {
    setError();
    return false;
  }
  entering_ = true;
  updateResultFromInput();
  return true;
}

void CalculatorState::updateExpression(const bool withEquals) {
  if (pending_ == Operator::None) {
    if (!withEquals) expression_[0] = '\0';
    return;
  }

  const char* right = percentApplied_ ? percentLabel_ : input_;
  if (entering_) {
    std::snprintf(expression_, sizeof(expression_), "%s %s %s%s", left_, operatorText(pending_), right,
                  withEquals ? " =" : "");
  } else {
    std::snprintf(expression_, sizeof(expression_), "%s %s", left_, operatorText(pending_));
  }
}

void CalculatorState::updateResultFromInput() { std::snprintf(result_, sizeof(result_), "%s", input_); }

void CalculatorState::setError() {
  error_ = true;
  pending_ = Operator::None;
  entering_ = false;
  justEvaluated_ = false;
  percentApplied_ = false;
  accumulator_ = 0.0;
  std::strcpy(input_, "0");
  std::strcpy(result_, "0");
  left_[0] = '\0';
  percentLabel_[0] = '\0';
  expression_[0] = '\0';
}

double CalculatorState::inputValue() const { return std::strtod(input_, nullptr); }

int CalculatorState::significantDigitCount(const char* text) {
  int count = 0;
  bool significant = false;
  for (; *text != '\0'; ++text) {
    if (*text < '0' || *text > '9') continue;
    if (*text != '0') significant = true;
    if (significant) ++count;
  }
  return count;
}

const char* CalculatorState::operatorText(const Operator op) {
  switch (op) {
    case Operator::None:
      return "";
    case Operator::Add:
      return "+";
    case Operator::Subtract:
      return "-";
    case Operator::Multiply:
      return "×";
    case Operator::Divide:
      return "÷";
  }
  return "";
}

bool CalculatorState::formatNumber(double value, char* output, const size_t outputSize) {
  if (!std::isfinite(value)) return false;
  if (value == 0.0) value = std::abs(value);
  const int written = std::snprintf(output, outputSize, "%.12g", value);
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

}  // namespace calculator
