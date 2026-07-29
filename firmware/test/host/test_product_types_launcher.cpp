#include <cassert>

#include "product/product_types.hpp"

int main() {
  static_assert(kProductVersion == "1.3.5l");
  assert(kProductVersion == "1.3.5l");
  return 0;
}
