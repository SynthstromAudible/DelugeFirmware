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
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/param_ids.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
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

/// @p address's param in @p owner's stack, made where @p create allows: a cable (with its source's default polarity), a
/// MIDI CC lane, an expression lane and the set that holds it.
ModelStackWithAutoParam* paramInOwner(const OwnerStack& owner, const project::ParamAddress& address, bool create) {
	if (owner.stack == nullptr) {
		return nullptr;
	}
	switch (address.space) {
	case project::ParamSpace::ENGINE:
		if (owner.holder == Holder::SOUND) {
			return instrument::is_cable_depth(address.id)
			           ? cableIn(owner.stack, address.id, create)
			           : paramIn(owner.stack, address.id, {params::Kind::PATCHED, params::Kind::UNPATCHED_SOUND});
		}
		return owner.holder == Holder::EFFECTABLE ? paramIn(owner.stack, address.id, {params::Kind::UNPATCHED_GLOBAL})
		                                          : nullptr;
	case project::ParamSpace::MIDI_CC:
		return owner.holder == Holder::MIDI_CLIP
		           ? paramInCollection(owner.stack, owner.stack->paramManager->getMIDIParamCollectionSummary(),
		                               static_cast<int32_t>(address.id.value), create)
		           : nullptr;
	case project::ParamSpace::EXPRESSION:
		if (owner.holder != Holder::MIDI_CLIP && owner.holder != Holder::SOUND) {
			return nullptr;
		}
		if (create) {
			(void)owner.stack->paramManager->ensureExpressionParamSetExists(address.owner.kind
			                                                                == project::OwnerId::Kind::DRUM);
		}
		return paramInCollection(owner.stack, owner.stack->paramManager->getExpressionParamSetSummary(),
		                         static_cast<int32_t>(address.id.value), create);
	}
	return nullptr;
}

/// The clip a write or a read of @p clip lands in while the arrangement records it: its take, which a write makes
/// (cloning @p clip once) and a read only follows; else @p clip itself.
::Clip* takeOf(::Song& song, ::Clip& clip, bool make, void* memory) {
	if (playbackHandler.recording != RecordingMode::ARRANGEMENT || !playbackHandler.isEitherClockActive()
	    || clip.isArrangementOnlyClip() || !song.isClipActive(&clip) || clip.output == nullptr) {
		return &clip;
	}
	::Clip* active = clip.output->getActiveClip();
	if (active != nullptr && active->beingRecordedFromClip == &clip) {
		return active;
	}
	if (!make) {
		return &clip;
	}
	ModelStackWithTimelineCounter* stack = setupModelStackWithSong(memory, &song)->addTimelineCounter(&clip);
	(void)clip.possiblyCloneForArrangementRecording(stack);
	return static_cast<::Clip*>(stack->getTimelineCounter());
}

/// @p address with its clip moved to @p clip: an INSTRUMENT or DRUM owner's clip id, or a CLIP owner's own id.
project::ParamAddress inClip(project::ParamAddress address, const ::Clip& clip) {
	if (address.owner.kind == project::OwnerId::Kind::CLIP) {
		address.owner.id = clip.id();
	}
	else {
		address.clip_id = clip.id();
	}
	return address;
}

/// The stack of an INSTRUMENT or DRUM owner with no clip: its backed-up params, which an arranger row edits.
OwnerStack backedOwnerStack(::Song& song, const project::ParamAddress& address, void* memory) {
	using Kind = project::OwnerId::Kind;
	if (address.owner.kind == Kind::DRUM) {
		::Drum* drum = song.drumForId(address.owner.id);
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return {};
		}
		auto* sound = static_cast<SoundDrum*>(drum);
		ParamManager* params = song.live.param_backup.getPreferablyWithClip(*drum, nullptr);
		return params != nullptr
		           ? OwnerStack{setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, sound, nullptr, params),
		                        Holder::SOUND}
		           : OwnerStack{};
	}
	if (address.owner.kind != Kind::INSTRUMENT) {
		return {};
	}
	::Output* output = outputWithId(song, address.owner.id);
	if (output == nullptr) {
		return {};
	}
	ParamManager* params = song.live.param_backup.getPreferablyWithClip(*output, nullptr);
	if (params == nullptr) {
		return {};
	}
	switch (output->type) {
	case OutputType::SYNTH:
		return {setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, static_cast<::SoundInstrument*>(output),
		                                                       nullptr, params),
		        Holder::SOUND};
	case OutputType::KIT:
		return {
		    setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, static_cast<::Kit*>(output), nullptr, params),
		    Holder::EFFECTABLE};
	case OutputType::AUDIO:
		return {setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, static_cast<::AudioOutput*>(output),
		                                                       nullptr, params),
		        Holder::EFFECTABLE};
	default:
		return {};
	}
}

/// The clip @p address names, by its INSTRUMENT/DRUM clip id or its CLIP owner's id; null for none.
::Clip* clipOf(::Song& song, const project::ParamAddress& address) {
	if (address.owner.kind == project::OwnerId::Kind::CLIP) {
		return song.clipForId(address.owner.id);
	}
	return address.clip_id ? song.clipForId(*address.clip_id) : nullptr;
}

/// The stack of @p owner: its clip's, or, with no clip, an INSTRUMENT or DRUM owner's backed-up params.
OwnerStack ownerOrBackedStack(::Song& song, const project::ParamAddress& owner, void* memory) {
	using Kind = project::OwnerId::Kind;
	return (owner.owner.kind == Kind::INSTRUMENT || owner.owner.kind == Kind::DRUM) && !owner.clip_id
	           ? backedOwnerStack(song, owner, memory)
	           : ownerStackAt(song, owner, memory);
}

} // namespace

ModelStackWithAutoParam* paramStackAt(::Song& song, const project::ParamAddress& address, void* memory) {
	return paramInOwner(ownerStackAt(song, address, memory), address, false);
}

ModelStackWithAutoParam* paramStackForWrite(::Song& song, project::ParamAddress& address, void* memory) {
	if (::Clip* clip = clipOf(song, address)) {
		address = inClip(address, *takeOf(song, *clip, true, memory));
	}
	return paramInOwner(ownerOrBackedStack(song, address, memory), address, true);
}

bool stutterAt(::Song& song, const project::ParamAddress& owner, bool begin) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const OwnerStack found = ownerOrBackedStack(song, owner, memory);
	if (found.stack == nullptr || found.holder == Holder::NONE || found.holder == Holder::MIDI_CLIP) {
		return false;
	}
	auto* audio = static_cast<ModControllableAudio*>(found.stack->modControllable);
	auto* params = static_cast<ParamManagerForTimeline*>(found.stack->paramManager);
	if (begin) {
		audio->beginStutter(params);
	}
	else {
		audio->endStutter(params);
	}
	if (found.holder == Holder::SOUND) {
		static_cast<::Sound*>(audio)->reassessRenderSkippingStatus(found.stack->addSoundFlags());
	}
	return true;
}

bool grabRegionAt(::Song& song, const project::ParamAddress& owner, uint32_t pos) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const OwnerStack found = ownerOrBackedStack(song, owner, memory);
	if (found.stack == nullptr || found.stack->paramManager == nullptr) {
		return false;
	}
	found.stack->paramManager->toForTimeline()->grabValuesFromPos(pos, found.stack);
	return true;
}

bool midiCcSelectAt(::Song& song, const project::ParamAddress& owner, int32_t knob, bool on) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const OwnerStack found = ownerStackAt(song, owner, memory);
	if (found.stack == nullptr || found.holder != Holder::MIDI_CLIP) {
		return false;
	}
	(void)found.stack->modControllable->modEncoderButtonAction(static_cast<uint8_t>(knob), on, found.stack);
	return true;
}

ModelStackWithAutoParam* paramStackFollowingTake(::Song& song, const project::ParamAddress& address, void* memory) {
	if (::Clip* clip = clipOf(song, address)) {
		const project::ParamAddress followed = inClip(address, *takeOf(song, *clip, false, memory));
		return paramInOwner(ownerStackAt(song, followed, memory), followed, false);
	}
	return paramStackAt(song, address, memory);
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
