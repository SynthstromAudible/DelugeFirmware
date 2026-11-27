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
#include <algorithm>
#include <utility>

void ConsequenceSectionReorder::swapSections(Song& song, uint8_t first_section, uint8_t second_section) {
	auto swap_section_number = [first_section, second_section](auto& section) {
		if (section == first_section || section == second_section) {
			section = (section == first_section) ? second_section : first_section;
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

void ConsequenceSectionReorder::moveSection(Song& song, uint8_t from_section, uint8_t to_section) {
	while (from_section != to_section) {
		auto next_section = static_cast<uint8_t>(from_section + (from_section < to_section ? 1 : -1));
		swapSections(song, from_section, next_section);
		from_section = next_section;
	}
}

Error ConsequenceSectionReorder::revert(TimeType time, ModelStack* model_stack) {
	if (time == TimeType::BEFORE) {
		moveSection(*model_stack->song, second_section_, first_section_);
	}
	else {
		moveSection(*model_stack->song, first_section_, second_section_);
	}
	// Keep the destination visible without moving a viewport that already shows it.
	int32_t destination_section = time == TimeType::BEFORE ? first_section_ : second_section_;
	auto minimum_scroll = std::max<int32_t>(0, destination_section - kDisplayHeight + 1);
	auto maximum_scroll = std::min<int32_t>(destination_section, kMaxNumSections - kDisplayHeight);
	model_stack->song->songGridScrollY =
	    std::clamp<int32_t>(model_stack->song->songGridScrollY, minimum_scroll, maximum_scroll);
	return Error::NONE;
}
