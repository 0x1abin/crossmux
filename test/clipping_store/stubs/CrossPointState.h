#pragma once
#include <string>
struct HostState {
  std::string openEpubPath;
  bool fail = false;
  bool saveToFile() { return !fail; }
};
inline HostState hostState;
#define APP_STATE hostState
