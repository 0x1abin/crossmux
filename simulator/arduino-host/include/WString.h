#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : data_(s ? s : "") {}
  // std::string conversions are explicit to avoid overload ambiguity at call sites that
  // also accept std::string_view (Arduino's String never converts implicitly from std::string).
  explicit String(const std::string& s) : data_(s) {}
  explicit String(std::string&& s) : data_(std::move(s)) {}
  String(char c) : data_(1, c) {}
  String(int v) : data_(std::to_string(v)) {}
  String(unsigned int v) : data_(std::to_string(v)) {}
  String(long v) : data_(std::to_string(v)) {}
  String(unsigned long v) : data_(std::to_string(v)) {}
  String(long long v) : data_(std::to_string(v)) {}
  String(unsigned long long v) : data_(std::to_string(v)) {}
  String(double v) : data_(std::to_string(v)) {}
  String(const String&) = default;
  String(String&&) noexcept = default;
  String& operator=(const String&) = default;
  String& operator=(String&&) noexcept = default;
  String& operator=(const char* s) {
    data_ = s ? s : "";
    return *this;
  }

  const char* c_str() const { return data_.c_str(); }
  size_t length() const { return data_.length(); }
  bool isEmpty() const { return data_.empty(); }
  void reserve(size_t n) { data_.reserve(n); }
  char charAt(size_t i) const { return i < data_.length() ? data_[i] : '\0'; }

  // ArduinoJson's generic reader consumes mutable String inputs byte by byte.
  int read() { return readPos_ < data_.size() ? static_cast<unsigned char>(data_[readPos_++]) : -1; }

  bool equals(const String& other) const { return data_ == other.data_; }
  bool equals(const char* other) const { return other && data_ == other; }

  bool startsWith(const String& prefix) const {
    return data_.length() >= prefix.data_.length() && data_.compare(0, prefix.data_.length(), prefix.data_) == 0;
  }
  bool startsWith(const char* prefix) const {
    if (!prefix) return false;
    size_t n = std::strlen(prefix);
    return data_.length() >= n && data_.compare(0, n, prefix) == 0;
  }

  bool endsWith(const String& suffix) const {
    return data_.length() >= suffix.data_.length() &&
           data_.compare(data_.length() - suffix.data_.length(), suffix.data_.length(), suffix.data_) == 0;
  }
  bool endsWith(const char* suffix) const {
    if (!suffix) return false;
    size_t n = std::strlen(suffix);
    return data_.length() >= n && data_.compare(data_.length() - n, n, suffix) == 0;
  }

  int indexOf(char c, size_t fromIndex = 0) const {
    auto p = data_.find(c, fromIndex);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int indexOf(const char* s, size_t fromIndex = 0) const {
    if (!s) return -1;
    auto p = data_.find(s, fromIndex);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }

  String substring(size_t from) const { return String(data_.substr(from)); }
  String substring(size_t from, size_t to) const {
    if (to <= from || from >= data_.length()) return String();
    return String(data_.substr(from, to - from));
  }

  void toLowerCase() {
    for (auto& c : data_) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  }
  void toUpperCase() {
    for (auto& c : data_) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
  }

  void trim() {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!data_.empty() && isSpace(static_cast<unsigned char>(data_.front()))) data_.erase(0, 1);
    while (!data_.empty() && isSpace(static_cast<unsigned char>(data_.back()))) data_.pop_back();
  }

  bool concat(const char* s) {
    if (!s) return false;
    data_ += s;
    return true;
  }
  bool concat(const String& other) {
    data_ += other.data_;
    return true;
  }

  // ArduinoJson v7 expects `write` overloads on String-shaped sinks.
  size_t write(uint8_t b) {
    data_ += static_cast<char>(b);
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) {
    data_.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

  long toInt() const {
    if (data_.empty()) return 0;
    char* end = nullptr;
    long v = std::strtol(data_.c_str(), &end, 10);
    return end == data_.c_str() ? 0 : v;
  }
  double toFloat() const {
    if (data_.empty()) return 0.0;
    char* end = nullptr;
    double v = std::strtod(data_.c_str(), &end);
    return end == data_.c_str() ? 0.0 : v;
  }

  String& operator+=(const String& other) {
    data_ += other.data_;
    return *this;
  }
  String& operator+=(const char* s) {
    if (s) data_ += s;
    return *this;
  }
  String& operator+=(char c) {
    data_ += c;
    return *this;
  }
  String& operator+=(int v) {
    data_ += std::to_string(v);
    return *this;
  }

  friend String operator+(String lhs, const String& rhs) {
    lhs += rhs;
    return lhs;
  }
  friend String operator+(String lhs, const char* rhs) {
    lhs += rhs;
    return lhs;
  }

  friend bool operator==(const String& a, const String& b) { return a.data_ == b.data_; }
  friend bool operator==(const String& a, const char* b) { return b && a.data_ == b; }
  friend bool operator==(const char* a, const String& b) { return a && b.data_ == a; }
  friend bool operator!=(const String& a, const String& b) { return !(a == b); }
  friend bool operator!=(const String& a, const char* b) { return !(a == b); }
  friend bool operator!=(const char* a, const String& b) { return !(a == b); }

 private:
  std::string data_;
  size_t readPos_ = 0;
};
