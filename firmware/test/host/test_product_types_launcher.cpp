#include <cassert>

#include "product/product_types.hpp"

int main() {
  static_assert(kProductVersion == "1.3.0l");
  assert(kProductVersion == "1.3.0l");
  return 0;
}
