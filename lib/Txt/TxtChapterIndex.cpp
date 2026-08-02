#include "TxtChapterIndex.h"

#include <array>
#include <cctype>

namespace txt_chapter_index {
namespace {

bool isAsciiSpace(const char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
}

std::string_view trim(std::string_view line) {
  while (!line.empty() && isAsciiSpace(line.front())) line.remove_prefix(1);
  while (!line.empty() && isAsciiSpace(line.back())) line.remove_suffix(1);
  constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";
  if (line.compare(0, UTF8_BOM.size(), UTF8_BOM) == 0) line.remove_prefix(UTF8_BOM.size());
  return line;
}

bool consume(std::string_view text, size_t& pos, const std::string_view token) {
  if (text.compare(pos, token.size(), token) != 0) return false;
  pos += token.size();
  return true;
}

void skipAsciiSpaces(const std::string_view text, size_t& pos) {
  while (pos < text.size() && isAsciiSpace(text[pos])) ++pos;
}

bool isChineseHeading(const std::string_view title) {
  constexpr std::array<std::string_view, 14> NUMERALS = {"零", "〇", "一", "二", "三", "四", "五",
                                                         "六", "七", "八", "九", "十", "百", "千"};
  constexpr std::array<std::string_view, 4> LARGE_NUMERALS = {"万", "亿", "两", "廿"};
  constexpr std::array<std::string_view, 6> UNITS = {"章", "节", "回", "卷", "部", "篇"};

  size_t pos = 0;
  if (!consume(title, pos, "第")) return false;
  skipAsciiSpaces(title, pos);

  bool hasNumber = false;
  while (pos < title.size()) {
    if (title[pos] >= '0' && title[pos] <= '9') {
      hasNumber = true;
      ++pos;
      continue;
    }
    bool matched = false;
    for (const auto numeral : NUMERALS) {
      if (consume(title, pos, numeral)) {
        matched = true;
        hasNumber = true;
        break;
      }
    }
    if (!matched) {
      for (const auto numeral : LARGE_NUMERALS) {
        if (consume(title, pos, numeral)) {
          matched = true;
          hasNumber = true;
          break;
        }
      }
    }
    if (!matched) break;
  }
  if (!hasNumber) return false;
  skipAsciiSpaces(title, pos);
  for (const auto unit : UNITS) {
    if (title.compare(pos, unit.size(), unit) == 0) return true;
  }
  return false;
}

bool equalsAsciiCaseInsensitive(const std::string_view text, const std::string_view expected) {
  if (text.size() < expected.size()) return false;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(text[i])) != expected[i]) return false;
  }
  return true;
}

bool isEnglishHeading(const std::string_view title) {
  constexpr std::array<std::string_view, 3> PREFIXES = {"chapter", "book", "part"};
  size_t pos = 0;
  for (const auto prefix : PREFIXES) {
    if (equalsAsciiCaseInsensitive(title, prefix) && title.size() > prefix.size() &&
        isAsciiSpace(title[prefix.size()])) {
      pos = prefix.size();
      break;
    }
  }
  if (pos == 0) return false;
  skipAsciiSpaces(title, pos);

  const size_t numberStart = pos;
  while (pos < title.size() && title[pos] >= '0' && title[pos] <= '9') ++pos;
  if (pos == numberStart) {
    while (pos < title.size()) {
      const char value = static_cast<char>(std::tolower(static_cast<unsigned char>(title[pos])));
      if (value != 'i' && value != 'v' && value != 'x' && value != 'l' && value != 'c' && value != 'd' &&
          value != 'm') {
        break;
      }
      ++pos;
    }
  }
  if (pos == numberStart) return false;
  if (pos == title.size() || isAsciiSpace(title[pos])) return true;
  return title[pos] == '.' || title[pos] == ':' || title[pos] == '-' || title.compare(pos, 3, "：") == 0 ||
         title.compare(pos, 3, "—") == 0;
}

}  // namespace

std::string_view chapterTitle(std::string_view line) {
  line = trim(line);
  if (line.empty() || line.size() >= TITLE_CAPACITY) return {};
  return isChineseHeading(line) || isEnglishHeading(line) ? line : std::string_view{};
}

}  // namespace txt_chapter_index
