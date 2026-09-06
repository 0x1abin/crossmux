#pragma once
#include <cassert>
inline int renderLockDepth = 0;
struct RenderLock {
  RenderLock() {
    assert(renderLockDepth == 0);
    ++renderLockDepth;
  }
  ~RenderLock() { --renderLockDepth; }
};
