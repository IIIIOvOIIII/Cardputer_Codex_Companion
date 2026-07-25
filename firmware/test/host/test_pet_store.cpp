#include <cassert>

#include "product/pet_store.hpp"

int main() {
  assert(inactive_pet_slot(PetSlot::a) == PetSlot::b);
  assert(inactive_pet_slot(PetSlot::b) == PetSlot::a);
  assert(inactive_pet_slot(PetSlot::none) == PetSlot::a);
  assert(select_boot_pet_slot(PetSlot::a, true, true) == PetSlot::a);
  assert(select_boot_pet_slot(PetSlot::a, false, true) == PetSlot::b);
  assert(select_boot_pet_slot(PetSlot::b, true, false) == PetSlot::a);
  assert(select_boot_pet_slot(PetSlot::none, true, false) == PetSlot::a);
  assert(select_boot_pet_slot(PetSlot::none, false, false) == PetSlot::none);
  assert(pet_commit_can_activate(true, true, true));
  assert(!pet_commit_can_activate(true, true, false));
  assert(!pet_commit_can_activate(false, true, true));
  return 0;
}
