#pragma once

#include <array>

#include "product/product_types.hpp"

inline constexpr std::size_t kProductRuntimeHeapReserveBytes = 44 * 1024;

class ProductStartupBackend {
 public:
  virtual ~ProductStartupBackend() = default;
  virtual bool display() = 0;
  virtual bool config() = 0;
  virtual bool keyboard() = 0;
  virtual bool ble() = 0;
  virtual bool wifi() = 0;
  virtual bool web() = 0;
  virtual bool companion() = 0;
};

class ProductController {
 public:
  explicit ProductController(ProductStartupBackend& backend)
      : backend_(backend) {}
  void start();
  [[nodiscard]] ServiceState state(BootStage stage) const {
    return states_[static_cast<std::size_t>(stage)];
  }

 private:
  ProductStartupBackend& backend_;
  std::array<ServiceState, 7> states_{};
};

#ifdef ESP_PLATFORM
void product_runtime_start();
#endif
