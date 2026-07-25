#include <cassert>

#include "product/pin_rotation.hpp"

int main() {
  PinRotationState state("12345678");
  state.restore(7);
  assert(!state.rotate("12345678", "1234abcd", 1000));
  assert(!state.rotate("wrong", "87654321", 1000));
  assert(state.rotate("12345678", "87654321", 1000));
  assert(state.revision() == 8);
  assert(state.next_pairing().has_value());
  assert(*state.next_pairing() == "87654321");
  assert(state.authorize("12345678", false, 1001) ==
         PinAuthorization::denied);
  assert(state.authorize("12345678", true, 300999) ==
         PinAuthorization::previous_companion_action);
  assert(state.authorize("12345678", true, 301000) ==
         PinAuthorization::denied);

  PinRotationState early("12345678");
  early.restore(2);
  assert(early.rotate("12345678", "87654321", 500));
  assert(early.authorize("87654321", false, 600) ==
         PinAuthorization::current);
  assert(!early.next_pairing().has_value());
  assert(early.authorize("12345678", true, 700) ==
         PinAuthorization::denied);
  assert(early.revision() == 3);
  early.restore(9);
  assert(early.revision() == 9);
  return 0;
}
