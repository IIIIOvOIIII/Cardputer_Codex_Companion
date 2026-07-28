#include "product/storage_compatibility.hpp"

#ifdef ESP_PLATFORM

#include "esp_partition.h"

StorageCompatibility inspect_storage_compatibility() {
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "storage");
  return evaluate_storage_compatibility(
      partition != nullptr,
      partition != nullptr && partition->type == ESP_PARTITION_TYPE_DATA,
      partition != nullptr &&
          partition->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
      partition == nullptr ? 0 : partition->size);
}

#endif
