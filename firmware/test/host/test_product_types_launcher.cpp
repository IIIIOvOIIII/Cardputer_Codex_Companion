#include <cassert>

#include "product/product_types.hpp"

int main() {
  static_assert(kProductVersion == "1.3.2l");
  assert(kProductVersion == "1.3.2l");
  return 0;
}
