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

#include "model/project_bridge/param_stack.h"

#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/param_ids.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <initializer_list>

namespace deluge::bridge {

namespace params = deluge::modulation::params;

namespace {

/// @p stack's param @p id in the first of @p kinds whose collection holds it, or null.
ModelStackWithAutoParam* paramIn(ModelStackWithThreeMainThings* stack, engine::ParamId id,
                                 std::initializer_list<params::Kind> kinds) {
	for (const params::Kind kind : kinds) {
		const std::optional<int32_t> index = runtimeIndexOf(kind, id);
		if (!index) {
			continue;
		}
		ModelStackWithAutoParam* found = kind == params::Kind::PATCHED ? stack->getPatchedAutoParamFromId(*index)
		                                                               : stack->getUnpatchedAutoParamFromId(*index);
		return found != nullptr && found->autoParam != nullptr ? found : nullptr;
	}
	return nullptr;
}

} // namespace

ModelStackWithAutoParam* paramStackAt(::Song& song, const project::ParamAddress& address, void* memory) {
	using Kind = project::OwnerId::Kind;
	if (address.space != project::ParamSpace::ENGINE) {
		return nullptr;
	}
	if (address.owner.kind == Kind::SONG) {
		return paramIn(song.setupModelStackWithSongAsTimelineCounter(memory), address.id,
		               {params::Kind::UNPATCHED_GLOBAL});
	}
	if (address.owner.kind == Kind::CLIP) {
		::Clip* clip = song.clipForId(address.owner.id);
		if (clip == nullptr || clip->type != ClipType::AUDIO || clip->output == nullptr) {
			return nullptr;
		}
		auto* audio = static_cast<::AudioClip*>(clip);
		ModelStackWithThreeMainThings* stack = setupModelStackWithThreeMainThingsButNoNoteRow(
		    memory, &song, static_cast<::AudioOutput*>(clip->output), clip, &audio->paramManager);
		return paramIn(stack, address.id, {params::Kind::UNPATCHED_GLOBAL});
	}
	if (!address.clip_id) {
		return nullptr;
	}
	::Clip* clip = song.clipForId(*address.clip_id);
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT || clip->output == nullptr) {
		return nullptr;
	}
	auto* instrumentClip = static_cast<::InstrumentClip*>(clip);
	::Output* output = clip->output;
	if (address.owner.kind == Kind::INSTRUMENT) {
		if (output->id() != address.owner.id) {
			return nullptr;
		}
		if (output->type == OutputType::SYNTH) {
			ModelStackWithThreeMainThings* stack = setupModelStackWithThreeMainThingsIncludingNoteRow(
			    memory, &song, clip, 0, nullptr, static_cast<::SoundInstrument*>(output),
			    &instrumentClip->paramManager);
			return paramIn(stack, address.id, {params::Kind::PATCHED, params::Kind::UNPATCHED_SOUND});
		}
		if (output->type == OutputType::KIT) {
			ModelStackWithThreeMainThings* stack = setupModelStackWithThreeMainThingsIncludingNoteRow(
			    memory, &song, clip, 0, nullptr, static_cast<::Kit*>(output), &instrumentClip->paramManager);
			return paramIn(stack, address.id, {params::Kind::UNPATCHED_GLOBAL});
		}
		return nullptr;
	}
	if (address.owner.kind == Kind::DRUM && output->type == OutputType::KIT) {
		::Drum* drum = song.drumForId(address.owner.id);
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return nullptr;
		}
		int32_t noteRowIndex = 0;
		::NoteRow* noteRow = instrumentClip->getNoteRowForDrum(drum, &noteRowIndex);
		if (noteRow == nullptr) {
			return nullptr;
		}
		ModelStackWithThreeMainThings* stack = setupModelStackWithThreeMainThingsIncludingNoteRow(
		    memory, &song, clip, noteRowIndex, noteRow, static_cast<::SoundDrum*>(drum), &noteRow->paramManager);
		return paramIn(stack, address.id, {params::Kind::PATCHED, params::Kind::UNPATCHED_SOUND});
	}
	return nullptr;
}

} // namespace deluge::bridge
