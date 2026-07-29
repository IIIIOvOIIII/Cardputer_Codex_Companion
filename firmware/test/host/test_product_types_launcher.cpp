#include <cassert>

#include "product/product_types.hpp"

int main() {
  static_assert(kProductVersion == "1.3.3l");
  assert(kProductVersion == "1.3.3l");
  return 0;
}
