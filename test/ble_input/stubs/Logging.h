#pragma once
inline unsigned testLogCount = 0;
template <typename... Args>
void testLog(Args...) {
  ++testLogCount;
}
#define LOG_INF(...) testLog(__VA_ARGS__)
#define LOG_ERR(...) testLog(__VA_ARGS__)
