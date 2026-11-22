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
/// @brief What a mod button's press or release does over the focused owner, beyond the popup's text: pure, so the
///        scenes and a spec read the same answer.

#include "ui/controls/knob_focus.h"

#include <cstdint>

namespace deluge::ui::controls {

/// @brief What the view tells a mod button's rules.
struct SectionInputs {
	bool root_is_performance{false};        ///< the root UI is performance view
	bool mod_button_ui_mode_in_range{true}; ///< the UI mode is one the mod buttons are offered in
	bool root_is_clip_minder{false};        ///< the root UI is a clip's view
	/// The owner's pages of the pressed section keep a playing stutter running (see section_keeps_stutter).
	bool keeps_stutter{false};
};

/// @brief What a mod button's press or release does.
struct SectionResult {
	bool change_mode{false};     ///< the knobs' owner takes the button as its mode
	bool end_stutter{false};     ///< the owner's stutter ends
	bool toggle_vu_meter{false}; ///< the song's VU meter shows, or stops showing, in place of the knobs' levels
	bool taken{false};           ///< the rules took the press: the button's popup shows
	[[nodiscard]] constexpr bool operator==(const SectionResult&) const = default;
};

/// @brief What pressing (@p on) or releasing mod button @p button does over @p focus.
/// @param focus  What the knobs point at.
/// @param mode   The owner's mode before the press.
/// @param button The mod button.
/// @param on     True for the press, false for the release.
/// @param in     What the view tells the rules.
/// @return Nothing for a press out of the button's UI modes off performance view. Else a press changes the mode of
///         an owner that has one and shows the popup, and a press of button 0 while it is the mode toggles the VU
///         meter outside a clip's view. Press or release ends the owner's stutter unless the section keeps it, and a
///         chain's (the song's or a clip's) on performance view neither.
[[nodiscard]] SectionResult resolve_section(const KnobFocus& focus, uint8_t mode, int32_t button, bool on,
                                            const SectionInputs& in);

} // namespace deluge::ui::controls
