#pragma once

#include <string>
#include <string_view>

// A single extra header sent with a request, on top of whatever auth/UA
// headers the transport already sets (e.g. a Cloudflare Access service token).
struct HttpHeader {
  std::string name;
  std::string value;
};

// "Name: Value" -> trimmed name/value. A missing colon treats the whole
// entry as a bare header name with no value.
void parseHttpHeaderLine(std::string_view line, HttpHeader& header);

// Inverse of parseHttpHeaderLine(); empty when the header is unset. With
// maskValue, a non-empty value is replaced by "******" for display.
std::string formatHttpHeaderLine(const HttpHeader& header, bool maskValue = false);

// Wire boundary: ASCII token names, no line breaks/control bytes in values.
// An empty name denotes an unset slot and is never a sendable header.
bool isValidHttpHeader(std::string_view name, std::string_view value);
inline bool isValidHttpHeader(const HttpHeader& header) { return isValidHttpHeader(header.name, header.value); }
