#include <cassert>

#include "product/product_types.hpp"

int main() {
  static_assert(kProductVersion == "1.3.4l");
  assert(kProductVersion == "1.3.4l");
  return 0;
}
