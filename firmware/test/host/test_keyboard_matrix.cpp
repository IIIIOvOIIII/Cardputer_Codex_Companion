#include <array>
#include <cassert>

#include "product/keyboard_matrix.hpp"

int main() {
  assert(kKeyboardScannerTaskStackBytes == 3328);

  assert(matrix_key_id(0, 0) == 43);  // selector 0, input 0 -> row 3 col 1
  assert(matrix_key_id(3, 6) == 13);  // selector 3, input 6 -> row 0 col 13
  assert(matrix_key_id(4, 0) == 42);  // selector 4, input 0 -> row 3 col 0
  assert(matrix_key_id(7, 6) == 12);  // selector 7, input 6 -> row 0 col 12

  KeyboardDebouncer debouncer;
  std::array<uint8_t, 8> raw{};
  std::array<MatrixKeyEvent, 8> events{};
  raw[7] = 1u << 6;  // row 0 col 12

  assert(debouncer.update(raw, 100, events) == 0);
  assert(debouncer.update(raw, 119, events) == 0);
  assert(debouncer.update(raw, 120, events) == 1);
  assert(events[0].physical_key == 12);
  assert(events[0].pressed);

  raw[7] = 0;
  assert(debouncer.update(raw, 130, events) == 0);
  assert(debouncer.update(raw, 150, events) == 1);
  assert(!events[0].pressed);
  return 0;
}
