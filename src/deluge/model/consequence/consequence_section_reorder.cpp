/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence_section_reorder.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include <utility>

void ConsequenceSectionReorder::swapSections(Song& song, uint8_t first_section, uint8_t second_section) {
	auto swap_section_number = [first_section, second_section](auto& section) {
		if (section == first_section) {
			section = second_section;
		}
		else if (section == second_section) {
			section = first_section;
		}
	};

	for (int32_t clip_index = 0; clip_index < song.sessionClips.getNumElements(); ++clip_index) {
		swap_section_number(song.sessionClips.getClipAtIndex(clip_index)->section);
	}
	// Repeat counts, MIDI launch commands and colours follow the section's clips.
	std::swap(song.sections[first_section], song.sections[second_section]);
	for (SessionMacro& macro : song.sessionMacros) {
		if (macro.kind == SessionMacroKind::SECTION) {
			swap_section_number(macro.section);
		}
	}
	// Special armed-section values (254 and 255) are not section IDs and stay unchanged.
	swap_section_number(session.lastSectionArmed);
}

Error ConsequenceSectionReorder::revert(TimeType /*time*/, ModelStack* model_stack) {
	swapSections(*model_stack->song, first_section_, second_section_);
	return Error::NONE;
}
