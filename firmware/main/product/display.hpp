#pragma once

#include "esp_err.h"
#include "product/ui_model.hpp"

esp_err_t display_start(UiModel* model);
void display_render_boot(const UiModel& model);
void display_render_runtime(const UiModel& model);
