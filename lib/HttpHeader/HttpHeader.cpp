#include "HttpHeader.h"

#include <AsciiText.h>

void parseHttpHeaderLine(const std::string_view line, HttpHeader& header) {
  const size_t colon = line.find(':');
  header.name = trimAsciiWhitespace(line.substr(0, colon));
  if (colon == std::string_view::npos) {
    header.value.clear();
  } else {
    header.value = trimAsciiWhitespace(line.substr(colon + 1));
  }
}

std::string formatHttpHeaderLine(const HttpHeader& header, const bool maskValue) {
  if (header.name.empty()) return "";
  if (header.value.empty()) return header.name;
  return header.name + ": " + (maskValue ? "******" : header.value);
}

bool isValidHttpHeader(const std::string_view name, const std::string_view value) {
  if (name.empty()) return false;
  for (const unsigned char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
    switch (c) {
      case '!':
      case '#':
      case '$':
      case '%':
      case '&':
      case '\'':
      case '*':
      case '+':
      case '-':
      case '.':
      case '^':
      case '_':
      case '`':
      case '|':
      case '~':
        break;
      default:
        return false;
    }
  }
  for (const unsigned char c : value) {
    if ((c < 0x20 && c != '\t') || c == 0x7F) return false;
  }
  return true;
}
