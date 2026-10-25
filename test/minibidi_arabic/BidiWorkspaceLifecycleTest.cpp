#include <BidiUtils.h>

#include <cassert>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace {
bool failWorkspace = true;
unsigned allocations = 0;
}  // namespace
void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
  ++allocations;
  return failWorkspace ? nullptr : std::malloc(bytes);
}

int main() {
  std::string output = "unchanged";
  std::vector<uint16_t> order{7};
  assert(!BidiUtils::startsWithRtl("Latin"));
  assert(BidiUtils::startsWithRtl("مرحبا"));
  assert(!BidiUtils::applyBidiVisual("", output));
  assert(!BidiUtils::computeVisualWordOrder({"one"}, false, order));
  assert(order.empty() && allocations == 0);
  assert(!BidiUtils::applyBidiVisual("مرحبا", output));
  assert(output == "unchanged" && allocations == 1);
  order = {7};
  assert(!BidiUtils::computeVisualWordOrder({"one", "مرحبا"}, true, order));
  assert(order.empty() && allocations == 2);
  failWorkspace = false;
  assert(BidiUtils::applyBidiVisual("مرحبا", output));
  assert(output != "unchanged" && allocations == 3);
  const std::string first = output;
  assert(BidiUtils::applyBidiVisual("مرحبا", output));
  assert(output == first && allocations == 3);
  assert(BidiUtils::computeVisualWordOrder({"one", "مرحبا"}, true, order));
  assert(order.size() == 2 && allocations == 3);
}
