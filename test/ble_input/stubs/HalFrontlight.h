#pragma once
struct TestFrontlight {
  bool present() const { return false; }
};
inline TestFrontlight Frontlight;
