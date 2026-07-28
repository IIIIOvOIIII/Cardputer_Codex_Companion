#include <cassert>

#include "product/storage_compatibility.hpp"

static_assert(
    evaluate_storage_compatibility(false, false, false, 0).state ==
    StorageCompatibilityState::missing);
static_assert(
    evaluate_storage_compatibility(true, false, true, 0x1e0000).state ==
    StorageCompatibilityState::wrong_type);
static_assert(
    evaluate_storage_compatibility(true, true, false, 0x1e0000).state ==
    StorageCompatibilityState::wrong_type);
static_assert(
    evaluate_storage_compatibility(true, true, true, 0x1dffff).state ==
    StorageCompatibilityState::too_small);
static_assert(
    evaluate_storage_compatibility(true, true, true, 0x1e0000).state ==
    StorageCompatibilityState::ready);

int main() {
  const StorageCompatibility ready =
      evaluate_storage_compatibility(true, true, true, 0x1e0000);
  assert(ready.ready());
  assert(ready.size_bytes == 0x1e0000);
  assert(storage_compatibility_name(ready.state) == "ready");
  assert(storage_compatibility_name(
             StorageCompatibilityState::missing) == "missing");
  assert(storage_compatibility_name(
             StorageCompatibilityState::wrong_type) == "wrong_type");
  assert(storage_compatibility_name(
             StorageCompatibilityState::too_small) == "too_small");
  return 0;
}
