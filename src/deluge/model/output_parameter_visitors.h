/*
 * Copyright © 2026 Sean Ditny
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

#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/params/param_collection.h"
#include "processing/sound/sound_drum.h"
#include <array>

// Visit the matching parameter collection in every clip sharing an output or drum.
template <typename Callback>
Error visitOutputParameters(Output* output, Song* song, ModControllable* owner, deluge::modulation::params::Kind kind,
                            int32_t id, const Callback& callback) {
	for (Clip* clip : AllClips::everywhere(song)) {
		if (clip->output != output) {
			continue;
		}
		NoteRow* row = nullptr;
		int32_t row_index = 0;
		auto* manager = &clip->paramManager;
		if (owner != output->toModControllable()) {
			if (output->type != OutputType::KIT) {
				continue;
			}
			row = static_cast<InstrumentClip*>(clip)->getNoteRowForDrum(static_cast<SoundDrum*>(owner), &row_index);
			if (!row) {
				continue;
			}
			manager = &row->paramManager;
		}
		std::array<char, MODEL_STACK_MAX_SIZE> memory_storage{};
		char* memory = memory_storage.data();
		auto* stack =
		    setupModelStackWithThreeMainThingsIncludingNoteRow(memory, song, clip, row_index, row, owner, manager);
		for (auto* summary = manager->summaries; summary->paramCollection; ++summary) {
			auto* collection = summary->paramCollection;
			if (collection->getParamKind() != kind) {
				continue;
			}
			Error error = callback(stack->addParamCollectionAndId(collection, summary, id), clip);
			if (error != Error::NONE) {
				return error;
			}
			break;
		}
	}
	return Error::NONE;
}
