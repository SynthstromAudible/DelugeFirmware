/*
 * Copyright © 2025 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "gui/context_menu/context_menu.h"
#include <cstddef>
#include <cstdint>

class Clip;

namespace deluge::gui::context_menu::clip_settings {

/// Entry in the shared tempo ratio table: reduced fraction N:D.
struct RatioEntry {
	uint16_t numerator;
	uint16_t denominator;
};

/// All unique reduced fractions N:D with N,D in [1,8], sorted ascending by N/D.
/// Range: 25% (1:4) to 400% (4:1).
inline constexpr RatioEntry kRatios[] = {
    {1, 4}, // 25%
    {2, 7}, // 29%
    {1, 3}, // 33%
    {3, 8}, // 38%
    {2, 5}, // 40%
    {3, 7}, // 43%
    {1, 2}, // 50%
    {4, 7}, // 57%
    {3, 5}, // 60%
    {5, 8}, // 63%
    {2, 3}, // 67%
    {5, 7}, // 71%
    {3, 4}, // 75%
    {4, 5}, // 80%
    {5, 6}, // 83%
    {6, 7}, // 86%
    {7, 8}, // 88%
    {1, 1}, // 100%
    {8, 7}, // 114%
    {7, 6}, // 117%
    {6, 5}, // 120%
    {5, 4}, // 125%
    {4, 3}, // 133%
    {7, 5}, // 140%
    {3, 2}, // 150%
    {8, 5}, // 160%
    {5, 3}, // 167%
    {7, 4}, // 175%
    {2, 1}, // 200%
    {7, 3}, // 233%
    {5, 2}, // 250%
    {8, 3}, // 267%
    {3, 1}, // 300%
    {7, 2}, // 350%
    {4, 1}, // 400%
};

inline constexpr size_t kNumRatios = sizeof(kRatios) / sizeof(kRatios[0]);

/// Index of 1:1 (100%) in kRatios.
inline constexpr size_t kRatioDefault = 17;

/// Single-level menu showing tempo ratio as percentage, scrollable via encoder.
/// Each entry maps to the closest clean integer ratio N:D (max 8).
class TempoRatioMenu final : public ContextMenu {

public:
	TempoRatioMenu() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }

	Clip* clip = nullptr;

	char const* getTitle() override;
	std::span<const char*> getOptions() override;
};

extern TempoRatioMenu tempoRatioMenu;

} // namespace deluge::gui::context_menu::clip_settings
