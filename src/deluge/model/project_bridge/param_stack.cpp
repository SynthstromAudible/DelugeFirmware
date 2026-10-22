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

#include "engines/deluge/cables.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/param_ids.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
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

/// @p stack's param @p id in the collection @p summary holds, made where @p create allows, or null.
ModelStackWithAutoParam* paramInCollection(ModelStackWithThreeMainThings* stack, ParamCollectionSummary* summary,
                                           int32_t id, bool create) {
	if (summary == nullptr || summary->paramCollection == nullptr) {
		return nullptr;
	}
	ModelStackWithAutoParam* found = summary->paramCollection->getAutoParamFromId(
	    stack->addParamCollectionAndId(summary->paramCollection, summary, id), create);
	return found != nullptr && found->autoParam != nullptr ? found : nullptr;
}

/// The cable of @p stack's sound at depth @p depth, made where @p create allows, or null. A sound with no cable set
/// has none.
ModelStackWithAutoParam* cableIn(ModelStackWithThreeMainThings* stack, engine::ParamId depth, bool create) {
	if (stack->paramManager->getPatchCableSetAllowJibberish() == nullptr) {
		return nullptr;
	}
	return paramInCollection(stack, stack->paramManager->getPatchCableSetSummary(),
	                         static_cast<int32_t>(cableWordOf(depth)), create);
}

/// What an address's owner is, for which params it holds.
enum class Holder : uint8_t {
	NONE,
	SOUND,      ///< a synth or a sound drum: patched and unpatched params, cables, expression
	EFFECTABLE, ///< a kit's own effects, an audio clip or the song: global unpatched params
	MIDI_CLIP,  ///< a MIDI clip: its CC lanes and its expression
};

struct OwnerStack {
	ModelStackWithThreeMainThings* stack{nullptr};
	Holder holder{Holder::NONE};
};

/// The engine stack of @p address's owner in @p song: the clip by id, the output or drum by id, the drum's note row in
/// that clip.
OwnerStack ownerStackAt(::Song& song, const project::ParamAddress& address, void* memory) {
	using Kind = project::OwnerId::Kind;
	if (address.owner.kind == Kind::SONG) {
		return {song.setupModelStackWithSongAsTimelineCounter(memory), Holder::EFFECTABLE};
	}
	if (address.owner.kind == Kind::CLIP) {
		::Clip* clip = song.clipForId(address.owner.id);
		if (clip == nullptr || clip->output == nullptr) {
			return {};
		}
		if (clip->type == ClipType::AUDIO) {
			auto* audio = static_cast<::AudioClip*>(clip);
			return {setupModelStackWithThreeMainThingsButNoNoteRow(
			            memory, &song, static_cast<::AudioOutput*>(clip->output), clip, &audio->paramManager),
			        Holder::EFFECTABLE};
		}
		if (clip->type == ClipType::INSTRUMENT && clip->output->type == OutputType::MIDI_OUT) {
			auto* instrumentClip = static_cast<::InstrumentClip*>(clip);
			return {setupModelStackWithThreeMainThingsIncludingNoteRow(memory, &song, clip, 0, nullptr,
			                                                           static_cast<::MIDIInstrument*>(clip->output),
			                                                           &instrumentClip->paramManager),
			        Holder::MIDI_CLIP};
		}
		return {};
	}
	if (!address.clip_id) {
		return {};
	}
	::Clip* clip = song.clipForId(*address.clip_id);
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT || clip->output == nullptr) {
		return {};
	}
	auto* instrumentClip = static_cast<::InstrumentClip*>(clip);
	::Output* output = clip->output;
	if (address.owner.kind == Kind::INSTRUMENT) {
		if (output->id() != address.owner.id) {
			return {};
		}
		if (output->type == OutputType::SYNTH) {
			return {setupModelStackWithThreeMainThingsIncludingNoteRow(memory, &song, clip, 0, nullptr,
			                                                           static_cast<::SoundInstrument*>(output),
			                                                           &instrumentClip->paramManager),
			        Holder::SOUND};
		}
		if (output->type == OutputType::KIT) {
			return {setupModelStackWithThreeMainThingsIncludingNoteRow(
			            memory, &song, clip, 0, nullptr, static_cast<::Kit*>(output), &instrumentClip->paramManager),
			        Holder::EFFECTABLE};
		}
		return {};
	}
	if (address.owner.kind == Kind::DRUM && output->type == OutputType::KIT) {
		::Drum* drum = song.drumForId(address.owner.id);
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return {};
		}
		int32_t noteRowIndex = 0;
		::NoteRow* noteRow = instrumentClip->getNoteRowForDrum(drum, &noteRowIndex);
		if (noteRow == nullptr) {
			return {};
		}
		return {setupModelStackWithThreeMainThingsIncludingNoteRow(memory, &song, clip, noteRowIndex, noteRow,
		                                                           static_cast<::SoundDrum*>(drum),
		                                                           &noteRow->paramManager),
		        Holder::SOUND};
	}
	return {};
}

} // namespace

ModelStackWithAutoParam* paramStackAt(::Song& song, const project::ParamAddress& address, void* memory) {
	const OwnerStack owner = ownerStackAt(song, address, memory);
	if (owner.stack == nullptr) {
		return nullptr;
	}
	switch (address.space) {
	case project::ParamSpace::ENGINE:
		if (owner.holder == Holder::SOUND) {
			return instrument::is_cable_depth(address.id)
			           ? cableIn(owner.stack, address.id, false)
			           : paramIn(owner.stack, address.id, {params::Kind::PATCHED, params::Kind::UNPATCHED_SOUND});
		}
		return owner.holder == Holder::EFFECTABLE ? paramIn(owner.stack, address.id, {params::Kind::UNPATCHED_GLOBAL})
		                                          : nullptr;
	case project::ParamSpace::MIDI_CC:
		return owner.holder == Holder::MIDI_CLIP
		           ? paramInCollection(owner.stack, owner.stack->paramManager->getMIDIParamCollectionSummary(),
		                               static_cast<int32_t>(address.id.value), false)
		           : nullptr;
	case project::ParamSpace::EXPRESSION:
		return owner.holder == Holder::MIDI_CLIP || owner.holder == Holder::SOUND
		           ? paramInCollection(owner.stack, owner.stack->paramManager->getExpressionParamSetSummary(),
		                               static_cast<int32_t>(address.id.value), false)
		           : nullptr;
	}
	return nullptr;
}

PatchCableSet* cableSetAt(::Song& song, const project::ParamAddress& owner, void* memory) {
	const OwnerStack sound = ownerStackAt(song, owner, memory);
	if (sound.stack == nullptr || sound.holder != Holder::SOUND) {
		return nullptr;
	}
	return sound.stack->paramManager->getPatchCableSetAllowJibberish();
}

ModelStackWithAutoParam* cableStackAt(::Song& song, const project::ParamAddress& address, Polarity polarity,
                                      void* memory) {
	if (address.space != project::ParamSpace::ENGINE || !instrument::is_cable_depth(address.id)) {
		return nullptr;
	}
	const OwnerStack owner = ownerStackAt(song, address, memory);
	if (owner.stack == nullptr || owner.holder != Holder::SOUND) {
		return nullptr;
	}
	if (ModelStackWithAutoParam* found = cableIn(owner.stack, address.id, false)) {
		return found;
	}
	ModelStackWithAutoParam* made = cableIn(owner.stack, address.id, true);
	if (made == nullptr) {
		return nullptr;
	}
	PatchCableSet* set = owner.stack->paramManager->getPatchCableSet();
	PatchSource source{};
	ParamDescriptor destination{};
	PatchCableSet::dissectParamId(made->paramId, &destination, &source);
	const uint8_t index = set->getPatchCableIndex(source, destination, nullptr, false);
	if (index == 255) {
		return nullptr;
	}
	set->setPolarity(index, polarity);
	return made;
}

} // namespace deluge::bridge
