#include <cassert>
#include <string>
#include <string_view>
#include <vector>

#include "product/boot_recovery.hpp"

class FakeResetBackend final : public BootRecoveryResetBackend {
 public:
  bool erase_namespace(std::string_view name) override {
    erased.emplace_back(name);
    return name != fail_namespace;
  }

  BootRecoveryStorageResult erase_storage() override {
    storage_attempted = true;
    return storage_result;
  }

  std::vector<std::string> erased;
  std::string fail_namespace;
  bool storage_attempted = false;
  BootRecoveryStorageResult storage_result =
      BootRecoveryStorageResult::erased;
};

int main() {
  BootRecoveryHoldDetector hold;
  assert(!hold.update(false, 900));
  assert(!hold.update(true, 1000));
  assert(!hold.update(true, 1599));
  assert(hold.update(true, 1600));
  assert(!hold.update(false, 1700));
  assert(!hold.update(true, 2000));
  assert(!hold.update(true, 2500));
  assert(hold.update(true, 2600));

  assert(boot_recovery_choice(false, false) ==
         BootRecoveryChoice::none);
  assert(boot_recovery_choice(true, false) ==
         BootRecoveryChoice::erase);
  assert(boot_recovery_choice(false, true) ==
         BootRecoveryChoice::cancel);
  assert(boot_recovery_choice(true, true) ==
         BootRecoveryChoice::none);

  FakeResetBackend success;
  const BootRecoveryResetResult reset = reset_companion_data(success);
  assert(reset.success);
  assert(reset.stage == BootRecoveryResetStage::complete);
  assert(success.storage_attempted);
  assert(success.erased.size() == kCompanionResetNamespaces.size());
  for (std::size_t index = 0;
       index < kCompanionResetNamespaces.size(); ++index) {
    assert(success.erased[index] == kCompanionResetNamespaces[index]);
  }

  FakeResetBackend legacy_launcher;
  legacy_launcher.storage_result = BootRecoveryStorageResult::absent;
  const BootRecoveryResetResult reset_without_storage =
      reset_companion_data(legacy_launcher);
  assert(reset_without_storage.success);
  assert(reset_without_storage.stage ==
         BootRecoveryResetStage::complete);
  assert(legacy_launcher.storage_attempted);

  FakeResetBackend namespace_failure;
  namespace_failure.fail_namespace = "product_tls";
  const BootRecoveryResetResult failed_namespace =
      reset_companion_data(namespace_failure);
  assert(!failed_namespace.success);
  assert(failed_namespace.stage ==
         BootRecoveryResetStage::product_tls);
  assert(!namespace_failure.storage_attempted);
  assert(namespace_failure.erased.size() == 3);

  FakeResetBackend storage_failure;
  storage_failure.storage_result = BootRecoveryStorageResult::failed;
  const BootRecoveryResetResult failed_storage =
      reset_companion_data(storage_failure);
  assert(!failed_storage.success);
  assert(failed_storage.stage ==
         BootRecoveryResetStage::storage);
  assert(storage_failure.storage_attempted);
  return 0;
}
