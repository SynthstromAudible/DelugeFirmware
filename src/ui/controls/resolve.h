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
/// @brief What a gold knob's turn or press does, resolved from the focused owner's pages: pure, so the scenes, a MIDI
///        follow and a spec all read the same answer.

#include "ui/controls/knob_focus.h"
#include "ui/controls/page.h"

#include <array>
#include <cstdint>
#include <variant>

namespace deluge::ui::controls {

/// @brief A turn of a slot: the param it moves, on the focused owner.
struct ParamWrite {
	project::ParamAddress address;
	[[nodiscard]] bool operator==(const ParamWrite&) const = default;
};

/// @brief A press that steps a setting of the focused owner to its next choice.
struct StepSetting {
	project::ParamAddress address; ///< the setting, an ENGINE-space id of the owner
	[[nodiscard]] bool operator==(const StepSetting&) const = default;
};

/// @brief A press that moves the section to another of its pages: the page, and the slots the knobs are on there.
struct SelectPage {
	uint32_t section{0};
	uint32_t page_id{0};
	std::array<Slot, plugin::kRemoteControlsCount> slots{};
	[[nodiscard]] bool operator==(const SelectPage&) const = default;
};

/// @brief A press the host carries out itself: no param of the owner names it.
enum class HostCommand : uint8_t {
	STUTTER_BEGIN,      ///< a stutter knob went down
	STUTTER_END,        ///< a stutter knob came up
	MIDI_CC_SELECT_ON,  ///< a MIDI knob went down: the select encoder picks its CC
	MIDI_CC_SELECT_OFF, ///< a MIDI knob came up
	REVERB_PRESET,      ///< the song's next reverb preset
};

/// @brief What a turn or a press does: nothing, a param write, a setting step, another page, or a host command.
using Target = std::variant<std::monostate, ParamWrite, StepSetting, SelectPage, HostCommand>;

/// @brief What a turn of slot @p slot does.
/// @param focus What the knobs point at: its owner is the owner written.
/// @param pages The focused owner's pages for the current section.
/// @param slot  The slot turned (on the Deluge, 0 the lower knob, 1 the upper).
/// @return A ParamWrite of the current page's slot on the focused owner (a SETTING-flagged id, the song compressor's,
///         is written by its setting id); nothing for an empty slot, a slot out of range, or no owner.
[[nodiscard]] Target resolve_turn(const KnobFocus& focus, const SectionPages& pages, int32_t slot);

/// @brief What a press (@p on) or a release of slot @p slot does.
/// @param focus What the knobs point at: its owner holds a stepped setting.
/// @param pages The focused owner's pages for the current section.
/// @param slot  The slot pressed.
/// @param on    True for the press, false for the release.
/// @return The slot's press action resolved: a release ends a hold or a CC select and does nothing else. NEXT_PAGE
///         moves the pressed slot to the section's next page of its group, and with it each other slot still on the
///         page the pressed one was found on; from a current page the cycle lacks, it moves to the group's first page
///         whose id follows the current one's, wrapping round. TOGGLE_GROUP enters the other group at its entry page.
[[nodiscard]] Target resolve_press(const KnobFocus& focus, const SectionPages& pages, int32_t slot, bool on);

} // namespace deluge::ui::controls
