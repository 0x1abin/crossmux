#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace txt_chapter_index {

constexpr size_t TITLE_CAPACITY = 192;

struct Record {
  uint32_t sourceOffset = 0;
  char title[TITLE_CAPACITY] = {};
};
static_assert(sizeof(Record) == 196);

// Returns the trimmed line when it is a supported chapter heading, or an empty
// view otherwise. The input must be UTF-8 (ASCII is valid UTF-8).
std::string_view chapterTitle(std::string_view line);

}  // namespace txt_chapter_index
