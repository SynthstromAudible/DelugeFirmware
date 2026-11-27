/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// @file
/// @brief The pages the gold knobs are on, as the controls read them: an owner's published pages, their params as
///        targets in the owner's spaces.

#include "plugin/types/remote_controls.h"
#include "project/param_address.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace deluge::ui::controls {

/// @brief One control of a page: what a turn moves and what a press does.
struct Slot {
	std::optional<project::ParamTarget> target;           ///< what a turn moves; none for an empty slot
	plugin::PressAction press{plugin::PressAction::NONE}; ///< what a press does
	std::optional<plugin::ParamId> press_param;           ///< the setting a STEP_SETTING press steps
	[[nodiscard]] constexpr bool operator==(const Slot&) const = default;
};

/// @brief One page of a section: its slots, and the group TOGGLE_GROUP moves between.
struct Page {
	uint32_t section{0}; ///< the section; on the Deluge, the mod button
	uint32_t page_id{0}; ///< stable within the owner's section
	uint8_t group{0};    ///< NEXT_PAGE stays within one group; TOGGLE_GROUP moves to the other
	std::array<Slot, plugin::kRemoteControlsCount> slots{};
	bool keeps_stutter{false}; ///< pressing this section's button leaves a playing stutter running
	[[nodiscard]] constexpr bool operator==(const Page&) const = default;
};

/// @brief The focused owner's pages for its current section.
struct SectionPages {
	Page current;                          ///< the page the knobs are on now, as the owner's records hold it
	std::vector<Page> cycle;               ///< the section's pages, in the order NEXT_PAGE walks them
	std::array<uint32_t, 2> group_entry{}; ///< the page id TOGGLE_GROUP enters each group at
};

/// @brief The page @p page publishes, its params as ENGINE-space targets.
[[nodiscard]] Page page_of(const plugin::RemotePage& page);

} // namespace deluge::ui::controls
