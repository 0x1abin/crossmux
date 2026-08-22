#pragma once
#include <HalStorage.h>

#include <iostream>
#include <utility>

namespace serialization {
constexpr uint32_t MAX_PATH_BYTES = 4096;
constexpr uint32_t MAX_TEXT_BYTES = 16384;

template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
[[nodiscard]] bool readPod(std::istream& is, T& value) {
  T next{};
  if (!is.read(reinterpret_cast<char*>(&next), sizeof(T))) return false;
  value = next;
  return true;
}

template <typename T>
[[nodiscard]] bool readPod(HalFile& file, T& value) {
  T next{};
  if (file.read(reinterpret_cast<uint8_t*>(&next), sizeof(T)) != static_cast<int>(sizeof(T))) return false;
  value = next;
  return true;
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

[[nodiscard]] inline bool readString(std::istream& is, std::string& s, const uint32_t maxLength) {
  uint32_t len = 0;
  if (!readPod(is, len) || len > maxLength) return false;
  std::string next;
  next.resize(len);
  if (len > 0 && !is.read(next.data(), len)) return false;
  s = std::move(next);
  return true;
}

[[nodiscard]] inline bool readString(HalFile& file, std::string& s, const uint32_t maxLength) {
  uint32_t len = 0;
  if (!readPod(file, len) || len > maxLength) return false;
  const size_t position = file.position();
  const size_t fileSize = file.size();
  if (position > fileSize || len > fileSize - position) return false;

  std::string next;
  next.resize(len);
  if (len > 0 && file.read(next.data(), len) != static_cast<int>(len)) return false;
  s = std::move(next);
  return true;
}
}  // namespace serialization
