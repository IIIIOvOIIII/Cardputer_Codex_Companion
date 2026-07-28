#include <cassert>

#include "product/product_types.hpp"

int main() {
  static_assert(kProductVersion == "1.3.1l");
  assert(kProductVersion == "1.3.1l");
  return 0;
}
