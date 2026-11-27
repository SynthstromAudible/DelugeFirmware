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
/// @brief What an owner publishes for hardware controls: sections of pages of up to eight params, in the shape of
///        CLAP's remote-controls extension (`clap.remote-controls/2`), with what pressing each slot's knob does.

#include "plugin/types/param.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::plugin {

/// @brief Slots per page: CLAP's `CLAP_REMOTE_CONTROLS_COUNT`.
inline constexpr std::size_t kRemoteControlsCount = 8;

/// @brief What pressing a slot's knob does. CLAP's pages carry no press; these stay on the Deluge side of the L3
///        adapter.
enum class PressAction : uint8_t {
	NONE,           ///< a press does nothing
	STEP_SETTING,   ///< step the setting param @c press_param to its next choice
	HOLD_STUTTER,   ///< stutter while held
	NEXT_PAGE,      ///< the section's next page in its group
	TOGGLE_GROUP,   ///< the section's page in its other group
	MIDI_CC_SELECT, ///< pick the knob's CC with the select encoder while held
	REVERB_PRESET,  ///< the song's next reverb preset
};

/// @brief One control of a page.
struct RemoteSlot {
	std::optional<ParamId> param;         ///< what a turn moves; none for an empty slot
	PressAction press{PressAction::NONE}; ///< what a press does
	std::optional<ParamId> press_param;   ///< the setting a STEP_SETTING press steps
	/// @brief Whether two slots turn, and press, alike: every field equal.
	[[nodiscard]] constexpr bool operator==(const RemoteSlot&) const = default;
};

/// @brief One page: CLAP's `clap_remote_controls_page`, plus each slot's press and the page's group.
struct RemotePage {
	uint32_t section{0};   ///< the section; on the Deluge, the mod button
	uint32_t page_id{0};   ///< stable within the owner's section
	uint8_t group{0};      ///< pages TOGGLE_GROUP moves between; NEXT_PAGE stays within one
	std::string_view name; ///< the page's name
	std::array<RemoteSlot, kRemoteControlsCount> slots{};
	bool for_preset{false}; ///< the sound's own page (its saved knobs), not the device's default
	/// Pressing this section's button leaves a playing stutter running. A Deluge-side extension, like PressAction.
	bool keeps_stutter{false};
	/// @brief Whether two pages are the same page with the same slots: every field equal, the name by its text.
	[[nodiscard]] constexpr bool operator==(const RemotePage&) const = default;
};

} // namespace deluge::plugin
