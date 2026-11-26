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
#include "model/fx/stutter_gesture.h"

#include "instruments/classic/cables.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit_cable.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/lane_target.h"
#include "model/project_bridge/param_ids.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/move_section.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/sound/mod_matrix_host.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <initializer_list>

namespace deluge::bridge {

namespace params = deluge::modulation::params;

namespace {

/// @p stack's param @p id in the first of @p kinds whose collection holds it, or null.
ModelStackWithAutoParam* paramIn(ModelStackWithThreeMainThings* stack, plugin::ParamId id,
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
ModelStackWithAutoParam* cableIn(ModelStackWithThreeMainThings* stack, plugin::ParamId depth, bool create) {
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
	NOTE_CLIP,  ///< a CV clip, a MIDI or gate drum's row, a melodic note row: its expression only
};

struct OwnerStack {
	ModelStackWithThreeMainThings* stack{nullptr};
	Holder holder{Holder::NONE};
	/// The object the stack's owner slot holds, typed.
	LaneOwner owner;
};

/// The engine stack of @p address's owner in @p song: the clip by id, the output or drum by id, the drum's note row in
/// that clip. Its owner is not typed yet (ownerStackAt types it).
OwnerStack owner_stack_at_untyped(::Song& song, const project::ParamAddress& address, void* memory) {
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
		if (clip->type == ClipType::INSTRUMENT && clip->output->type == OutputType::CV) {
			auto* instrumentClip = static_cast<::InstrumentClip*>(clip);
			return {setupModelStackWithThreeMainThingsIncludingNoteRow(memory, &song, clip, 0, nullptr,
			                                                           deluge::model::owner_of(*clip->output),
			                                                           &instrumentClip->paramManager),
			        Holder::NOTE_CLIP};
		}
		return {};
	}
	if (address.owner.kind == Kind::NOTE_ROW) {
		::Clip* clip = song.clipForId(address.owner.id);
		if (clip == nullptr || clip->type != ClipType::INSTRUMENT || clip->output == nullptr
		    || clip->output->type == OutputType::KIT) {
			return {};
		}
		auto* instrumentClip = static_cast<::InstrumentClip*>(clip);
		int32_t noteRowIndex = 0;
		::NoteRow* noteRow = instrumentClip->getNoteRowForYNote(address.owner.row, &noteRowIndex);
		if (noteRow == nullptr) {
			return {};
		}
		return {setupModelStackWithThreeMainThingsIncludingNoteRow(memory, &song, clip, address.owner.row, noteRow,
		                                                           deluge::model::owner_of(*clip->output),
		                                                           &noteRow->paramManager),
		        Holder::NOTE_CLIP};
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
		if (drum == nullptr) {
			return {};
		}
		int32_t noteRowIndex = 0;
		::NoteRow* noteRow = instrumentClip->getNoteRowForDrum(drum, &noteRowIndex);
		if (noteRow == nullptr) {
			return {};
		}
		return {setupModelStackWithThreeMainThingsIncludingNoteRow(
		            memory, &song, clip, noteRowIndex, noteRow, deluge::model::owner_of(*drum), &noteRow->paramManager),
		        drum->type == DrumType::SOUND ? Holder::SOUND : Holder::NOTE_CLIP};
	}
	return {};
}

/// @p address's param in @p owner's stack, made where @p create allows: a MIDI CC lane, an expression lane and the set
/// that holds it, and, where @p make_cable allows too, a cable (with its source's default polarity).
ModelStackWithAutoParam* paramInOwner(const OwnerStack& owner, const project::ParamAddress& address, bool create,
                                      bool make_cable) {
	if (owner.stack == nullptr) {
		return nullptr;
	}
	switch (address.space) {
	case project::ParamSpace::ENGINE:
		if (owner.holder == Holder::SOUND) {
			return instruments::classic::is_cable_depth(address.id)
			           ? cableIn(owner.stack, address.id, create && make_cable)
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
		if (owner.holder != Holder::MIDI_CLIP && owner.holder != Holder::SOUND && owner.holder != Holder::NOTE_CLIP) {
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

/// @p address with its clip moved to @p clip: an INSTRUMENT or DRUM owner's clip id, or a CLIP or NOTE_ROW owner's own.
project::ParamAddress inClip(project::ParamAddress address, const ::Clip& clip) {
	if (address.owner.kind == project::OwnerId::Kind::CLIP || address.owner.kind == project::OwnerId::Kind::NOTE_ROW) {
		address.owner.id = clip.id();
	}
	else {
		address.clip_id = clip.id();
	}
	return address;
}

/// The stack of an INSTRUMENT or DRUM owner with no clip: its backed-up params, which an arranger row edits. Its owner
/// is not typed yet (backedOwnerStack types it).
OwnerStack backed_owner_stack_untyped(::Song& song, const project::ParamAddress& address, void* memory) {
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
	if (output->type == OutputType::MIDI_OUT || output->type == OutputType::CV) {
		// A MIDI or CV track's own params are its active clip's.
		ParamManager* own = output->getParamManager(&song);
		return own != nullptr ? OwnerStack{setupModelStackWithThreeMainThingsButNoNoteRow(
		                                       memory, &song, deluge::model::owner_of(*output), nullptr, own),
		                                   output->type == OutputType::MIDI_OUT ? Holder::MIDI_CLIP : Holder::NOTE_CLIP}
		                      : OwnerStack{};
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

/// The typed owner of the params @p found names: the object its stack's owner slot holds.
LaneOwner lane_owner_of(::Song& song, const project::ParamAddress& address, const OwnerStack& found) {
	using Kind = project::OwnerId::Kind;
	if (found.stack == nullptr) {
		return std::monostate{};
	}
	switch (address.owner.kind) {
	case Kind::SONG:
		return &song.live.master_channel;
	case Kind::CLIP:
	case Kind::NOTE_ROW:
		if (::Clip* clip = song.clipForId(address.owner.id); clip != nullptr && clip->output != nullptr) {
			return owner_of(*clip->output);
		}
		return std::monostate{};
	case Kind::INSTRUMENT:
		if (::Output* output = outputWithId(song, address.owner.id)) {
			return owner_of(*output);
		}
		return std::monostate{};
	case Kind::DRUM:
		if (::Drum* drum = song.drumForId(address.owner.id)) {
			return owner_of(*drum);
		}
		return std::monostate{};
	}
	return std::monostate{};
}

/// The engine stack of @p address's owner in @p song, with its owner typed.
OwnerStack ownerStackAt(::Song& song, const project::ParamAddress& address, void* memory) {
	OwnerStack found = owner_stack_at_untyped(song, address, memory);
	found.owner = lane_owner_of(song, address, found);
	return found;
}

/// The stack of an INSTRUMENT or DRUM owner's backed-up params, with its owner typed.
OwnerStack backedOwnerStack(::Song& song, const project::ParamAddress& address, void* memory) {
	OwnerStack found = backed_owner_stack_untyped(song, address, memory);
	found.owner = lane_owner_of(song, address, found);
	return found;
}

/// The lane @p found names in @p owner's stack, at @p address; none where nothing was found.
std::optional<LaneTarget> lane_of(const OwnerStack& owner, ModelStackWithAutoParam* found,
                                  const project::ParamAddress& address) {
	if (found == nullptr) {
		return std::nullopt;
	}
	return target_of(*found, address);
}

/// The clip @p address names, by its INSTRUMENT/DRUM clip id or its CLIP or NOTE_ROW owner's id; null for none.
::Clip* clipOf(::Song& song, const project::ParamAddress& address) {
	if (address.owner.kind == project::OwnerId::Kind::CLIP || address.owner.kind == project::OwnerId::Kind::NOTE_ROW) {
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

/// @p owner moved to the take the arrangement records its clip into, where there is one; never made here.
project::ParamAddress followingTake(::Song& song, project::ParamAddress owner, void* memory) {
	if (::Clip* clip = clipOf(song, owner)) {
		owner = inClip(owner, *takeOf(song, *clip, false, memory));
	}
	return owner;
}

} // namespace

std::optional<LaneTarget> lane_at(::Song& song, const project::ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const OwnerStack owner = ownerStackAt(song, address, memory);
	return lane_of(owner, paramInOwner(owner, address, false, false), address);
}

std::optional<LaneTarget> lane_for_write(::Song& song, project::ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (::Clip* clip = clipOf(song, address)) {
		address = inClip(address, *takeOf(song, *clip, true, memory));
	}
	const OwnerStack owner = ownerOrBackedStack(song, address, memory);
	return lane_of(owner, paramInOwner(owner, address, true, true), address);
}

std::optional<LaneTarget> lane_exact(::Song& song, const project::ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const OwnerStack owner = ownerOrBackedStack(song, address, memory);
	return lane_of(owner, paramInOwner(owner, address, true, false), address);
}

std::optional<LaneTarget> lane_following_take(::Song& song, const project::ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const project::ParamAddress followed = followingTake(song, address, memory);
	const OwnerStack owner = ownerOrBackedStack(song, followed, memory);
	return lane_of(owner, paramInOwner(owner, followed, false, false), followed);
}

std::optional<LaneTarget> sound_lane_exact(::Song& song, const project::ParamAddress& owner) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const OwnerStack found = ownerOrBackedStack(song, owner, memory);
	if (found.stack == nullptr || found.holder != Holder::SOUND) {
		return std::nullopt;
	}
	return LaneTarget{.address = owner,
	                  .song = found.stack->song,
	                  .timeline = found.stack->getTimelineCounterAllowNull(),
	                  .note_row_id = found.stack->noteRowId,
	                  .note_row = found.stack->getNoteRowAllowNull(),
	                  .owner = found.owner,
	                  .manager = found.stack->paramManager};
}

ModelStackWithAutoParam* paramStackAt(::Song& song, const project::ParamAddress& address, void* memory) {
	const std::optional<LaneTarget> lane = lane_at(song, address);
	return lane ? stack_of(*lane, memory) : nullptr;
}

ModelStackWithAutoParam* paramStackForWrite(::Song& song, project::ParamAddress& address, void* memory) {
	const std::optional<LaneTarget> lane = lane_for_write(song, address);
	return lane ? stack_of(*lane, memory) : nullptr;
}

std::optional<uint32_t> takeFor(::Song& song, uint32_t output_id) {
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->id() == output_id) {
			::Clip* active = output->getActiveClip();
			if (active == nullptr) {
				return std::nullopt;
			}
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			return takeOf(song, *active, true, memory)->id();
		}
	}
	return std::nullopt;
}

ModelStackWithAutoParam* paramStackExact(::Song& song, const project::ParamAddress& address, void* memory) {
	const std::optional<LaneTarget> lane = lane_exact(song, address);
	return lane ? stack_of(*lane, memory) : nullptr;
}

ModelStackWithAutoParam* paramStackOnRender(::Song& song, const project::ParamAddress& address, void* memory) {
	return paramInOwner(ownerStackAt(song, address, memory), address, false, false);
}

ModelStackWithThreeMainThings* soundStackExact(::Song& song, const project::ParamAddress& owner, void* memory) {
	const std::optional<LaneTarget> lane = sound_lane_exact(song, owner);
	return lane ? owner_stack_of(*lane, memory) : nullptr;
}

bool stutterAt(::Song& song, const project::ParamAddress& owner, bool begin) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const OwnerStack found = ownerOrBackedStack(song, followingTake(song, owner, memory), memory);
	if (found.stack == nullptr || (found.holder != Holder::SOUND && found.holder != Holder::EFFECTABLE)) {
		return false;
	}
	const model::EffectsOwner effects = *deluge::model::effects_owner_of(found.stack->owner);
	auto* params = static_cast<ParamManagerForTimeline*>(found.stack->paramManager);
	if (begin) {
		model::beginStutter(effects, *params);
	}
	else {
		model::endStutter(params);
	}
	if (found.holder == Holder::SOUND) {
		effects.sound->reassessRenderSkippingStatus(found.stack->addSoundFlags());
	}
	return true;
}

bool grabRegionAt(::Song& song, const project::ParamAddress& owner, uint32_t pos) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	// A held pad's region is a clip's (or the song's): an arranger row's backup has no position to grab from.
	const OwnerStack found = ownerStackAt(song, followingTake(song, owner, memory), memory);
	// `View::setModRegion` grabs only where the owner holds its main params.
	if (found.stack == nullptr || found.stack->paramManager == nullptr
	    || !found.stack->paramManager->containsAnyMainParamCollections()) {
		return false;
	}
	// The lanes' cursors are the render's: grabbed with the render held off
	deluge::playback::MoveSection section{"grabRegionAt"};
	found.stack->paramManager->toForTimeline()->grabValuesFromPos(pos, found.stack);
	return true;
}

ModelStackWithAutoParam* paramStackFollowingTake(::Song& song, const project::ParamAddress& address, void* memory) {
	const std::optional<LaneTarget> lane = lane_following_take(song, address);
	return lane ? stack_of(*lane, memory) : nullptr;
}

std::optional<ParamLane> paramLaneFollowingTake(::Song& song, const project::ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const ModelStackWithAutoParam* stack = paramStackFollowingTake(song, address, memory);
	if (stack == nullptr || stack->paramCollection == nullptr) {
		return std::nullopt;
	}
	return ParamLane{.kind = stack->paramCollection->getParamKind(), .id = stack->paramId};
}

std::optional<int32_t> paramValueAt(::Song& song, const project::ParamAddress& address, uint32_t pos) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = paramStackFollowingTake(song, address, memory);
	if (stack == nullptr || stack->autoParam == nullptr) {
		return std::nullopt;
	}
	return stack->autoParam->getValuePossiblyAtPos(static_cast<int32_t>(pos), stack);
}

ModelStackWithThreeMainThings* knobOwnerStackAt(::Song& song, const ui::controls::KnobFocus& focus, void* memory) {
	if (const auto* midi = std::get_if<ui::controls::MidiFocus>(&focus.owner); midi != nullptr && !midi->clip) {
		::Output* output = outputWithId(song, midi->track.output_id);
		return output != nullptr && output->type == OutputType::MIDI_OUT
		           ? setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, deluge::model::owner_of(*output),
		                                                            nullptr, output->getParamManager(&song))
		           : nullptr;
	}
	const std::optional<project::ParamAddress> owner = ui::controls::knob_owner(focus);
	if (!owner) {
		return nullptr;
	}
	return ownerOrBackedStack(song, followingTake(song, *owner, memory), memory).stack;
}

PatchCableSet* cableSetAt(::Song& song, const project::ParamAddress& owner, void* memory) {
	const OwnerStack sound = ownerStackAt(song, owner, memory);
	if (sound.stack == nullptr || sound.holder != Holder::SOUND) {
		return nullptr;
	}
	return sound.stack->paramManager->getPatchCableSetAllowJibberish();
}

std::optional<LaneTarget> cable_lane_at(::Song& song, const project::ParamAddress& address, Polarity polarity) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (address.space != project::ParamSpace::ENGINE || !instruments::classic::is_cable_depth(address.id)) {
		return std::nullopt;
	}
	const OwnerStack owner = ownerStackAt(song, address, memory);
	if (owner.stack == nullptr || owner.holder != Holder::SOUND) {
		return std::nullopt;
	}
	if (ModelStackWithAutoParam* found = cableIn(owner.stack, address.id, false)) {
		return lane_of(owner, found, address);
	}
	ModelStackWithAutoParam* made = cableIn(owner.stack, address.id, true);
	if (made == nullptr) {
		return std::nullopt;
	}
	PatchCableSet* set = owner.stack->paramManager->getPatchCableSet();
	PatchSource source{};
	deluge::synth::CableTarget destination{};
	PatchCableSet::dissectParamId(made->paramId, &destination, &source);
	const uint8_t index = set->getPatchCableIndex(source, destination);
	if (index == 255) {
		return std::nullopt;
	}
	ModMatrixHost matrix{*std::get<Sound*>(owner.owner), *made->paramManager};
	if (std::optional<CableEdit> edit =
	        matrix.setPolarity(index, static_cast<deluge::synth::CablePolarity>(polarity))) {
		deluge::edits::recordCableEdit(made, std::move(*edit));
	}
	return lane_of(owner, made, address);
}

ModelStackWithAutoParam* cableStackAt(::Song& song, const project::ParamAddress& address, Polarity polarity,
                                      void* memory) {
	const std::optional<LaneTarget> lane = cable_lane_at(song, address, polarity);
	return lane ? stack_of(*lane, memory) : nullptr;
}

} // namespace deluge::bridge
