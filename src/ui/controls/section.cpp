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

#include "ui/controls/section.h"

#include <utility>
#include <variant>

namespace deluge::ui::controls {

SectionResult resolve_section(const KnobFocus& focus, uint8_t mode, int32_t button, bool on, const SectionInputs& in) {
	if (on && !in.mod_button_ui_mode_in_range && !in.root_is_performance) {
		return {};
	}
	SectionResult out;
	if (on) {
		out.toggle_vu_meter = !in.root_is_clip_minder && std::cmp_equal(mode, button) && button == 0;
		out.change_mode = knob_mode_owner(focus).has_value();
		out.taken = true;
	}
	// A section whose pages keep a playing stutter leaves it running, and so does performance view, except for a
	// sound's: legacy's `Sound::modButtonAction` ends it on every view, and nothing stops a sound's knobs holding the
	// focus under a performance root (the root sets the song's only as it opens).
	const bool sound = std::holds_alternative<SoundFocus>(focus.owner);
	const bool has_owner =
	    sound || std::holds_alternative<ChainFocus>(focus.owner) || std::holds_alternative<SongFocus>(focus.owner);
	out.end_stutter = has_owner && !in.keeps_stutter && (sound || !in.root_is_performance);
	return out;
}

} // namespace deluge::ui::controls
