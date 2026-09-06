#pragma once
template <typename... Args>
void testLog(Args...) {}
#define LOG_INF(...) testLog(__VA_ARGS__)
#define LOG_ERR(...) testLog(__VA_ARGS__)
