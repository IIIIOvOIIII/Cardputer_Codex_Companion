#pragma once

#include <string_view>

#include "esp_err.h"
#include "product/ui_model.hpp"
#include "product/pet_store.hpp"

esp_err_t display_start(UiModel* model);
void display_render_boot(const UiModel& model);
void display_render_boot_recovery_prompt();
void display_render_boot_recovery_result(
    bool success,
    std::string_view stage
);
void display_render_page(const UiModel& model);
bool display_render_pet_frame(PetStore& store, PetState state,
                              uint8_t frame_index);
void display_render_placeholder(PetState state);
