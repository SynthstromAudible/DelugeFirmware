#pragma once

/// @file
/// @brief How a menu shows a param's stored word, per its descriptor's display: legacy's value scaling
/// (`gui/menu_item/value_scaling.cpp`), whose literals and shifts these reproduce.

#include "engine-api/types/param.h"

#include <cstdint>
#include <utility>

namespace deluge::scenes::bind {

/// @brief The menu value of stored word @p stored in @p display: the whole word range onto 0..50 (STANDARD), a word
/// that is never negative onto 0..50 (HALF_PRECISION), the whole word range onto -25..25 (PAN).
/// @param display The param's display.
/// @param stored  The stored word.
/// @return The menu value.
[[nodiscard]] int32_t menu_of(engine::ParamDisplay display, int32_t stored);

/// @brief The stored word menu value @p menu stands for in @p display: the inverse of menu_of, its ends the word's.
/// @param display The param's display.
/// @param menu    The menu value, within menu_range(display).
/// @return The stored word.
[[nodiscard]] int32_t stored_of(engine::ParamDisplay display, int32_t menu);

/// @brief The menu values @p display shows: 0..50, or -25..25 for PAN.
/// @param display The param's display.
/// @return The least and greatest menu values.
[[nodiscard]] std::pair<int32_t, int32_t> menu_range(engine::ParamDisplay display);

} // namespace deluge::scenes::bind
