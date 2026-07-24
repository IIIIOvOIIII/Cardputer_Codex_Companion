#include "product/product_controller.hpp"

extern "C" void ble_hid_task_start_up() {
  // The product keyboard matrix task owns HID report production.
}

extern "C" void app_main() {
  product_runtime_start();
}
