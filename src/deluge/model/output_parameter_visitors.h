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

// Visit the manager's collection of this kind, if it has one.
template <typename Callback>
Error visitManagerParameter(ModelStackWithThreeMainThings* stack, ParamManager* manager, Clip* clip,
                            deluge::modulation::params::Kind kind, int32_t id, const Callback& callback) {
	for (auto* summary = manager->summaries; summary->paramCollection; ++summary) {
		auto* collection = summary->paramCollection;
		if (collection->getParamKind() == kind) {
			return callback(stack->addParamCollectionAndId(collection, summary, id), clip);
		}
	}
	return Error::NONE;
}

// Visit the owner's backed-up managers, e.g. removed kit rows. Their Clip may be null.
template <typename Callback>
Error visitBackedUpParameters(Song* song, ModControllable* owner, deluge::modulation::params::Kind kind, int32_t id,
                              const Callback& callback) {
	for (int32_t i = 0; i < song->backedUpParamManagers.getNumElements(); ++i) {
		auto* backup = static_cast<BackedUpParamManager*>(song->backedUpParamManagers.getElementAddress(i));
		if (backup->modControllable != owner) {
			continue;
		}
		std::array<char, MODEL_STACK_MAX_SIZE> memory_storage{};
		auto* stack = setupModelStackWithThreeMainThingsButNoNoteRow(memory_storage.data(), song, owner, backup->clip,
		                                                             &backup->paramManager);
		Error error = visitManagerParameter(stack, &backup->paramManager, backup->clip, kind, id, callback);
		if (error != Error::NONE) {
			return error;
		}
	}
	return Error::NONE;
}

// Visit the matching parameter collection in every clip sharing an output or drum. With include_backups,
// also visit the owner's backed-up managers. Those aren't playing, so only callers which prepare or bind
// values include them.
template <typename Callback>
Error visitOutputParameters(Output* output, Song* song, ModControllable* owner, deluge::modulation::params::Kind kind,
                            int32_t id, const Callback& callback, bool include_backups = false) {
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
		Error error = visitManagerParameter(stack, manager, clip, kind, id, callback);
		if (error != Error::NONE) {
			return error;
		}
	}
	return include_backups ? visitBackedUpParameters(song, owner, kind, id, callback) : Error::NONE;
}
