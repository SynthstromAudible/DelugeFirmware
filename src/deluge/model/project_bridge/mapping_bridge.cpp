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

#include "model/project_bridge/mapping_bridge.h"

#include "io/midi/mapping_index.h"
#include "libdeluge/system.h"
#include "memory/heaps.h"
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/arrangement.h"
#include "project/mapping_codec.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>

namespace deluge::bridge {

using deluge::project::ControlsAddress;
using deluge::project::Mapping;
using deluge::project::MappingTable;
using deluge::project::MidiCommand;

namespace {

/// The learned knobs @p output holds outside the table: a synth's sound's, a kit's or an audio track's; null for any
/// other output.
[[nodiscard]] deluge::model::KnobCarrier* knobsOf(::Output& output) {
	const std::optional<deluge::model::EffectsOwner> effects =
	    deluge::model::effects_owner_of(deluge::model::owner_of(output));
	return effects ? effects->knobs : nullptr;
}

/// The learned knobs @p drum holds outside the table: a sound row's; null for any other row.
[[nodiscard]] deluge::model::KnobCarrier* knobsOf(::Drum& drum) {
	return drum.type == DrumType::SOUND ? static_cast<SoundDrum&>(drum).effectsOwner()->knobs : nullptr;
}

/// Record @p learned as @p target's binding, when it holds something and the table holds nothing for @p target.
void recordLearned(::Song& song, const LearnedMIDI& learned, deluge::project::MappingTarget target) {
	if (learned.containsSomething() && song.project.mappings.find(target) == nullptr) {
		recordLoadedMapping(song, Mapping{.source = command_of(learned), .target = target});
	}
}

/// Whether @p knob turns an engine or an arp param: one a file names.
bool turnsNamedParam(const Mapping& knob) {
	const deluge::project::ParamSpace space = std::get<deluge::project::OwnerParamTarget>(knob.target).param.space;
	return space == deluge::project::ParamSpace::ENGINE || space == deluge::project::ParamSpace::ARP;
}

/// Give each hosted device an engine or arp param's knob of @p address names in @p mappings a device entry, as a load
/// makes one for each device a song names: the device list shows it, and the index finds its cable once it is plugged
/// in.
void makeKnobDevices(const MappingTable& mappings, ControlsAddress address) {
	for (const Mapping& knob : mappings.params_of(address)) {
		if (knob.source.device && turnsNamedParam(knob)) {
			(void)cable_for(*knob.source.device);
		}
	}
}

/// Put the learned knobs an owner holds outside the table (@p knobs) into @p song's table as @p address's, when the
/// table holds none for @p address, and empty them: the table holds them from now on. Each device they name has its
/// entry.
void recordKnobs(ControlsAddress address, deluge::model::KnobCarrier& knobs, ::Song& song) {
	MappingTable& mappings = song.project.mappings;
	if (!knobs.loaded.empty() && mappings.params_of(address).empty()) {
		for (Mapping& knob : knobs.loaded) {
			std::get<deluge::project::OwnerParamTarget>(knob.target).owner = address;
		}
		mappings.set_params(address, knobs.loaded);
	}
	knobs.loaded.clear();
	makeKnobDevices(mappings, address);
}

/// Record @p drum's bindings: a sound row's knobs, its mute and its input.
void recordDrumBindings(Drum& drum, ::Song& song) {
	if (!drum.id()) {
		return;
	}
	if (drum.type == DrumType::SOUND) {
		recordKnobs({ControlsAddress::Kind::DRUM, *drum.id()}, *knobsOf(drum), song);
	}
	recordLearned(song, drum.muteMIDICommand, deluge::project::DrumMuteTarget{*drum.id()});
	recordLearned(song, drum.midiInput, deluge::project::LaneInputTarget{*drum.id()});
}

/// Whether @p output brings any binding of its own: a learned knob it holds outside the table, its input, or a kit's
/// row's knob, mute or input. A fresh output brings none, and recording it touches nothing: an auto-overdub's audio
/// track is made on the song's tick.
bool bringsBindings(Output& output) {
	if (const deluge::model::KnobCarrier* knobs = knobsOf(output); knobs != nullptr && !knobs->loaded.empty()) {
		return true;
	}
	if (output.type == OutputType::AUDIO) {
		return false;
	}
	if (static_cast<Instrument&>(output).midiInput.containsSomething()) {
		return true;
	}
	if (output.type != OutputType::KIT) {
		return false;
	}
	for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->muteMIDICommand.containsSomething() || drum->midiInput.containsSomething()
		    || (drum->type == DrumType::SOUND && !knobsOf(*drum)->loaded.empty())) {
			return true;
		}
	}
	return false;
}

/// Record @p output's bindings: its own knobs, its input, and a kit's rows' knobs, mutes and inputs.
void recordOutputBindings(Output& output, ::Song& song) {
	if (!output.id() || !bringsBindings(output)) {
		return;
	}
	const ControlsAddress address{ControlsAddress::Kind::OUTPUT, *output.id()};
	switch (output.type) {
	case OutputType::SYNTH:
	case OutputType::KIT:
		recordKnobs(address, *knobsOf(output), song);
		break;
	case OutputType::AUDIO:
		recordKnobs(address, *knobsOf(output), song);
		return; // an audio track has no MIDI input
	default:
		break;
	}
	recordLearned(song, static_cast<Instrument&>(output).midiInput, deluge::project::TrackInputTarget{*output.id()});
	if (output.type != OutputType::KIT) {
		return;
	}
	for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
		recordDrumBindings(*drum, song);
	}
}

/// Whether @p a and @p b play the same command from the same cable.
bool sameLearned(const LearnedMIDI& a, const LearnedMIDI& b) {
	return a.cable == b.cable && a.channelOrZone == b.channelOrZone && a.noteOrCC == b.noteOrCC;
}

/// The kit row @p target names, when it is a row's mute or input.
std::optional<uint32_t> rowOf(const deluge::project::MappingTarget& target) {
	if (const auto* mute = std::get_if<deluge::project::DrumMuteTarget>(&target)) {
		return mute->drum_id;
	}
	if (const auto* input = std::get_if<deluge::project::LaneInputTarget>(&target)) {
		return input->drum_id;
	}
	return std::nullopt;
}

/// Make @p target's entry (a row's mute or input) the command @p field holds after the runtime moved it there.
///
/// Its source is the one @p before held for @p target itself, else for the same kind of command of one of @p fromRows,
/// when that plays what @p field holds: so a device the runtime could not name stays named, and no other kind of
/// binding lends its own. Failing both, what @p field names.
void recordMovedCommand(MappingTable& mappings, std::span<const Mapping> before,
                        const deluge::project::MappingTarget& target, const LearnedMIDI& field,
                        std::span<const uint32_t> fromRows) {
	if (!field.containsSomething()) {
		mappings.unlearn(target);
		return;
	}
	const Mapping* played = nullptr;
	for (const Mapping& entry : before) {
		if (entry.target.index() != target.index() || !sameLearned(learned_of(entry.source), field)) {
			continue;
		}
		if (entry.target == target) {
			played = &entry;
			break;
		}
		const std::optional<uint32_t> row = rowOf(entry.target);
		if (played == nullptr && row && std::ranges::find(fromRows, *row) != fromRows.end()) {
			played = &entry;
		}
	}
	mappings.learn(Mapping{.source = played != nullptr ? played->source : command_of(field), .target = target});
}

/// The clip of @p song, in the session or the arrangement, whose id is @p id, or nullptr.
Clip* clipWithId(::Song& song, uint32_t id) {
	for (Clip* clip : AllClips::everywhere(&song)) {
		if (clip->id() == id) {
			return clip;
		}
	}
	return nullptr;
}

/// Make @p field the command the table holds for @p target, or empty when it holds none.
void projectCommand(const MappingTable& mappings, const deluge::project::MappingTarget& target, LearnedMIDI& field) {
	if (const Mapping* entry = mappings.find(target)) {
		field = field_of(entry->source);
	}
	else {
		field.clear();
	}
}

/// Drop the takeover state of @p owner's knobs: the store is the current song's, so another song's table leaves it be.
void forgetTakeover(const ::Song& song, ControlsAddress owner) {
	if (&song == currentSong) {
		deluge::midi::takeoverStore().forget_owner(owner);
	}
}

/// Give @p to the takeover state of @p from's knobs, as the table's copy gives it their entries.
void cloneTakeover(const ::Song& song, ControlsAddress from, ControlsAddress to) {
	if (&song == currentSong) {
		deluge::midi::takeoverStore().clone_owner(from, to);
	}
}

} // namespace

void recordLoadedMapping(::Song& song, const Mapping& mapping) {
	song.project.mappings.learn(mapping);
}

namespace {

/// A table write the render made, held for the app loop (see applyPendingMappingOps).
struct PendingOp {
	enum class Kind : uint8_t { CLONE_OUTPUT, FORGET_CLIP, FORGET_OUTPUT, FORGET_DRUM, ARRIVING_OUTPUT, ARRIVING_DRUM };
	Kind kind;
	uint32_t id;                 ///< the clip, output or drum; a clone's source
	uint32_t to{0};              ///< a clone's output
	const ::Song* song{nullptr}; ///< whose table: the song current when the render made the write
};

/// The table writes the render made: written only on the render (its one producer), read with it excluded.
struct PendingOps {
	std::array<PendingOp, kPendingMappingOps> ops{};
	std::atomic<size_t> count{0};
	std::atomic<bool> overflowed{false};
};

PendingOps& pendingOps() {
	static PendingOps pending;
	return pending;
}

/// Hold @p op for the app loop, in order; past the queue's room, note that some were lost. Called only on the render
/// (holdForAppLoop): the push is not atomic, so a second producer could overwrite a slot.
void enqueue(PendingOp op) {
	PendingOps& pending = pendingOps();
	const size_t n = pending.count.load(std::memory_order_acquire);
	if (n < kPendingMappingOps) {
		pending.ops[n] = op;
		pending.count.store(n + 1, std::memory_order_release);
	}
	else {
		pending.overflowed.store(true, std::memory_order_release);
	}
	deluge::markMappingIndexStale();
}

/// Stop the render at a table write it must not make, in a build that checks.
void requireAppLoop() {
	if constexpr (ALPHA_OR_BETA_VERSION) {
		if (onRenderTier()) {
			FREEZE_WITH_ERROR("E465");
		}
	}
}

void cloneNow(::Song& song, uint32_t from, uint32_t to) {
	Output* clone = outputWithId(song, to);
	if (clone == nullptr) {
		return; // the clone left the song before its copy landed: there is no one to copy to
	}
	if (deluge::model::KnobCarrier* knobs = knobsOf(*clone)) {
		knobs->cloned_from.reset(); // the copy is made here
	}
	if (song.project.mappings.params_of({ControlsAddress::Kind::OUTPUT, from}).empty()) {
		return;
	}
	song.project.mappings.copy_params(from, to);
	cloneTakeover(song, {ControlsAddress::Kind::OUTPUT, from}, {ControlsAddress::Kind::OUTPUT, to});
	makeKnobDevices(song.project.mappings, {ControlsAddress::Kind::OUTPUT, to});
}

/// Make the copy of its source's knobs for each clone the render made whose table holds none (the render's held copy
/// may be lost), while the source's entries are still there: before any held forget lands. Walks the song's outputs
/// and writes their clone marks: run it with the render excluded.
void copyMarkedClones(::Song& song) {
	for (Output* first :
	     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
		for (Output* output = first; output != nullptr; output = output->next) {
			deluge::model::KnobCarrier* knobs = knobsOf(*output);
			if (knobs == nullptr || !knobs->cloned_from || !output->id()) {
				continue;
			}
			const uint32_t from = *knobs->cloned_from;
			knobs->cloned_from.reset();
			if (song.project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *output->id()}).empty()) {
				cloneNow(song, from, *output->id());
			}
		}
	}
}

/// The ids of the outputs (playing or hibernating), kit rows and clips (session or arrangement) a song holds: collected
/// with the render excluded, then sorted with it going.
struct HeldIds {
	std::vector<uint32_t> outputs;
	std::vector<uint32_t> drums;
	std::vector<uint32_t> clips;

	/// Room for as many as @p like's lists held, and some more, so the collection does not allocate.
	explicit HeldIds(const std::array<size_t, 3>& like) {
		outputs.reserve(like[0] + 8);
		drums.reserve(like[1] + 64);
		clips.reserve(like[2] + 16);
	}

	[[nodiscard]] std::array<size_t, 3> sizes() const { return {outputs.size(), drums.size(), clips.size()}; }

	/// Collect @p song's ids. Only walks and copies: run it with the render excluded.
	void collect(::Song& song) {
		for (Output* first :
		     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
			for (Output* output = first; output != nullptr; output = output->next) {
				if (output->id()) {
					outputs.push_back(*output->id());
				}
				if (output->type != OutputType::KIT) {
					continue;
				}
				for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
					if (drum->id()) {
						drums.push_back(*drum->id());
					}
				}
			}
		}
		for (Clip* clip : AllClips::everywhere(&song)) {
			clips.push_back(clip->id());
		}
	}

	void sort() {
		std::ranges::sort(outputs);
		std::ranges::sort(drums);
		std::ranges::sort(clips);
	}

	[[nodiscard]] static bool has(const std::vector<uint32_t>& ids, uint32_t id) {
		return std::ranges::binary_search(ids, id);
	}
};

/// Bring @p song's table into line with what the queue lost, once the clones' copies are made (copyMarkedClones): drop
/// every entry whose owner the song no longer holds, then record what each output and kit row brings that the table
/// holds nothing for (an arrival's bindings), as a load's sweep does. The render is excluded only while the song's
/// lists are walked: it never writes the table, so the entries are matched against the ids the walk found with it
/// going.
void reconcileWithSong(::Song& song) {
	// The sizes of the last reconcile's lists: written and read on the app loop only.
	static std::array<size_t, 3> lastSizes{};
	std::optional<HeldIds> held{std::in_place, lastSizes};
	{
		AudioExcludedSection section; // the song's tick can splice the lists walked
		held->collect(song);
	}
	held->sort();
	lastSizes = held->sizes();
	MappingTable& mappings = song.project.mappings;
	std::vector<uint32_t> clips;
	std::vector<uint32_t> outputs;
	std::vector<uint32_t> drums;
	for (const Mapping& entry : mappings.entries()) {
		std::visit(
		    [&]<typename T>(const T& t) {
			    if constexpr (std::is_same_v<T, deluge::project::OwnerParamTarget>) {
				    if (t.owner.kind == ControlsAddress::Kind::OUTPUT && !HeldIds::has(held->outputs, t.owner.id)) {
					    outputs.push_back(t.owner.id);
				    }
				    else if (t.owner.kind == ControlsAddress::Kind::DRUM && !HeldIds::has(held->drums, t.owner.id)) {
					    drums.push_back(t.owner.id);
				    }
			    }
			    else if constexpr (std::is_same_v<T, deluge::project::ClipMuteTarget>) {
				    if (!HeldIds::has(held->clips, t.clip_id)) {
					    clips.push_back(t.clip_id);
				    }
			    }
			    else if constexpr (std::is_same_v<T, deluge::project::DrumMuteTarget>
			                       || std::is_same_v<T, deluge::project::LaneInputTarget>) {
				    if (!HeldIds::has(held->drums, t.drum_id)) {
					    drums.push_back(t.drum_id);
				    }
			    }
			    else if constexpr (std::is_same_v<T, deluge::project::TrackInputTarget>) {
				    if (!HeldIds::has(held->outputs, t.output_id)) {
					    outputs.push_back(t.output_id);
				    }
			    }
		    },
		    entry.target);
	}
	for (std::vector<uint32_t>* gone : {&clips, &outputs, &drums}) {
		std::ranges::sort(*gone);
		const auto [first, last] = std::ranges::unique(*gone);
		gone->erase(first, last);
	}
	for (uint32_t id : clips) {
		mappings.forget_clip(id);
	}
	for (uint32_t id : outputs) {
		mappings.forget_output(id);
		forgetTakeover(song, {ControlsAddress::Kind::OUTPUT, id});
	}
	for (uint32_t id : drums) {
		mappings.forget_drum(id);
		forgetTakeover(song, {ControlsAddress::Kind::DRUM, id});
	}
	AudioExcludedSection section; // the bindings are read from the outputs and rows the tick can splice
	for (Output* first :
	     {song.live.media.first_output, static_cast<Output*>(song.live.media.first_hibernating_instrument)}) {
		for (Output* output = first; output != nullptr; output = output->next) {
			recordOutputBindings(*output, song);
		}
	}
}

} // namespace

namespace {

/// How many RowCommandMoves are open: written and read on the app loop only.
int32_t rowCommandMovesOpen = 0;

} // namespace

bool onRenderTier() {
	return deluge_in_audio_context() || deluge::memory::inRenderBlock();
}

namespace {

/// Whether a write to @p song's table is held for the app loop: every write the render makes, which is always to the
/// current song's. A song swapped out is torn down after AudioEngine::retire hands it over, when the removal of its
/// outputs and rows finds them in no current song's records and the deletion of its clips forgets nothing, so no
/// write the render makes names another song; a build that checks stops at one (E468).
bool heldForAppLoop(const ::Song& song) {
	if (!onRenderTier()) {
		return false;
	}
	if constexpr (ALPHA_OR_BETA_VERSION) {
		if (&song != currentSong) {
			FREEZE_WITH_ERROR("E468");
		}
	}
	return &song == currentSong;
}

/// Hold @p op, a write to @p song's table, for the app loop when the render makes it. The queue's only way in, so the
/// render is its only producer.
/// @return Whether it was held; if not, the caller writes the table itself.
bool holdForAppLoop(::Song& song, PendingOp op) {
	if (!heldForAppLoop(song)) {
		return false;
	}
	op.song = &song;
	enqueue(op);
	return true;
}

/// Note that the app loop wrote @p song's table: the index, which is the current song's, is stale when @p song is it.
/// Another song (one a load is building) is indexed once it is made current, which marks the index itself.
void tableEdited(const ::Song& song) {
	if (&song == currentSong) {
		deluge::markMappingIndexStale();
	}
}

} // namespace

void recordArrivingOutput(::Output& output, ::Song& song) {
	if (!bringsBindings(output)) {
		return; // an auto-overdub's audio track, made on the song's tick, brings nothing
	}
	if (heldForAppLoop(song)) {
		if (output.id()) {
			(void)holdForAppLoop(song, {.kind = PendingOp::Kind::ARRIVING_OUTPUT, .id = *output.id()});
		}
		return;
	}
	applyPendingMappingOps(song);
	recordOutputBindings(output, song);
	tableEdited(song);
}

void recordArrivingDrum(::Drum& drum, ::Song& song) {
	if (heldForAppLoop(song)) {
		if (drum.id()) {
			(void)holdForAppLoop(song, {.kind = PendingOp::Kind::ARRIVING_DRUM, .id = *drum.id()});
		}
		return;
	}
	applyPendingMappingOps(song);
	recordDrumBindings(drum, song);
	tableEdited(song);
}

void cloneOutputMappings(const ::Output& from, ::Output& to, ::Song& song) {
	if (!from.id() || !to.id()) {
		return;
	}
	if (holdForAppLoop(song, {.kind = PendingOp::Kind::CLONE_OUTPUT, .id = *from.id(), .to = *to.id()})) {
		if (deluge::model::KnobCarrier* knobs = knobsOf(to)) {
			knobs->cloned_from = *from.id(); // the reconcile's way to the copy, should the held one be lost
		}
		return;
	}
	applyPendingMappingOps(song);
	cloneNow(song, *from.id(), *to.id());
	tableEdited(song);
}

void forgetClipMappings(::Song& song, uint32_t clip) {
	if (holdForAppLoop(song, {.kind = PendingOp::Kind::FORGET_CLIP, .id = clip})) {
		return;
	}
	applyPendingMappingOps(song);
	song.project.mappings.forget_clip(clip);
	tableEdited(song);
}

void forgetOutputMappings(::Song& song, uint32_t output) {
	if (holdForAppLoop(song, {.kind = PendingOp::Kind::FORGET_OUTPUT, .id = output})) {
		return;
	}
	applyPendingMappingOps(song);
	song.project.mappings.forget_output(output);
	forgetTakeover(song, {ControlsAddress::Kind::OUTPUT, output});
	tableEdited(song);
}

void forgetDrumMappings(::Song& song, uint32_t drum) {
	if (holdForAppLoop(song, {.kind = PendingOp::Kind::FORGET_DRUM, .id = drum})) {
		return;
	}
	applyPendingMappingOps(song);
	song.project.mappings.forget_drum(drum);
	forgetTakeover(song, {ControlsAddress::Kind::DRUM, drum});
	tableEdited(song);
}

bool mappingOpsHeld() {
	const PendingOps& pending = pendingOps();
	return pending.count.load(std::memory_order_acquire) != 0 || pending.overflowed.load(std::memory_order_acquire);
}

bool applyPendingMappingOps(::Song& song, OverflowReconcile reconcile) {
	if (&song != currentSong) {
		return false; // the render holds writes for the current song only
	}
	requireAppLoop();
	if (rowCommandMovesOpen > 0) {
		return false; // a kit change is moving its rows' commands: the writes wait for it (RowCommandMoves)
	}
	PendingOps& pending = pendingOps();
	if (pending.count.load(std::memory_order_acquire) == 0 && !pending.overflowed.load(std::memory_order_acquire)) {
		return false;
	}
	bool overflowed = false;
	{
		// The render writes the queue, and the ops walk and write what the tick can change. An overflow's reconcile,
		// after, excludes it only where it must.
		AudioExcludedSection section;
		const size_t n = pending.count.exchange(0, std::memory_order_acq_rel);
		overflowed = pending.overflowed.exchange(false, std::memory_order_acq_rel);
		if (overflowed) {
			// A clone's held copy may be among the writes lost: it is made before the held forgets land, here and on
			// each drain until the reconcile runs (a deferred one leaves the loss noted).
			copyMarkedClones(song);
		}
		MappingTable& mappings = song.project.mappings;
		for (size_t i = 0; i < n; i++) {
			const PendingOp op = pending.ops[i];
			if (op.song != &song) {
				continue; // made for the song a swap replaced, after it dropped the queue
			}
			switch (op.kind) {
			case PendingOp::Kind::CLONE_OUTPUT:
				cloneNow(song, op.id, op.to);
				break;
			case PendingOp::Kind::FORGET_CLIP:
				mappings.forget_clip(op.id);
				break;
			case PendingOp::Kind::FORGET_OUTPUT:
				mappings.forget_output(op.id);
				forgetTakeover(song, {ControlsAddress::Kind::OUTPUT, op.id});
				break;
			case PendingOp::Kind::FORGET_DRUM:
				mappings.forget_drum(op.id);
				forgetTakeover(song, {ControlsAddress::Kind::DRUM, op.id});
				break;
			case PendingOp::Kind::ARRIVING_OUTPUT:
				if (Output* output = outputWithId(song, op.id)) {
					recordOutputBindings(*output, song);
				}
				break;
			case PendingOp::Kind::ARRIVING_DRUM:
				if (Drum* drum = drumWithId(song, op.id)) {
					recordDrumBindings(*drum, song);
				}
				break;
			}
		}
	}
	deluge::markMappingIndexStale();
	if (!overflowed) {
		return false;
	}
	if (reconcile == OverflowReconcile::DEFER) {
		// The caller holds the render off: the reconcile waits for the next drain, and the index stays stale for it.
		pending.overflowed.store(true, std::memory_order_release);
		return true;
	}
	// Which writes were lost, and in what order, is not known: the table takes what the song now holds.
	reconcileWithSong(song);
	return false;
}

void discardPendingMappingOps() {
	AudioExcludedSection section; // the render writes the queue
	PendingOps& pending = pendingOps();
	pending.count.store(0, std::memory_order_release);
	pending.overflowed.store(false, std::memory_order_release);
}

void handOverInstrument(::Song& song, uint32_t from, uint32_t to, bool knobs) {
	applyPendingMappingOps(song); // a write the render made first lands first
	tableEdited(song);
	MappingTable& mappings = song.project.mappings;
	if (knobs) {
		mappings.retarget_output(from, to);
		cloneTakeover(song, {ControlsAddress::Kind::OUTPUT, from}, {ControlsAddress::Kind::OUTPUT, to});
		forgetTakeover(song, {ControlsAddress::Kind::OUTPUT, from});
		return;
	}
	const Mapping* input = mappings.find(deluge::project::TrackInputTarget{from});
	const std::optional<MidiCommand> source = input != nullptr ? std::optional{input->source} : std::nullopt;
	mappings.unlearn(deluge::project::TrackInputTarget{to});
	mappings.unlearn(deluge::project::TrackInputTarget{from});
	if (source) {
		mappings.learn(Mapping{.source = *source, .target = deluge::project::TrackInputTarget{to}});
	}
}

void projectOutputMappings(::Song& song, uint32_t output) {
	applyPendingMappingOps(song); // the projection plays the table with the render's writes in it
	// An owner with no knobs (a MIDI or CV track) or no input (an audio track) refuses its half: nothing to play.
	(void)projectMapping(song, deluge::project::OwnerParamTarget{{ControlsAddress::Kind::OUTPUT, output}, {}});
	(void)projectMapping(song, deluge::project::TrackInputTarget{output});
	deluge::midi::rebuildMappingIndex(song);
}

void projectDeviceMappings(::Song& song) {
	std::vector<deluge::project::MappingTarget> targets;
	std::vector<deluge::project::MidiDeviceRef> devices;
	for (const Mapping& entry : song.project.mappings.entries()) {
		if (!entry.source.device || entry.source.device->port != deluge::project::MidiDeviceRef::Port::HOSTED) {
			continue;
		}
		devices.push_back(*entry.source.device);
		if (std::holds_alternative<deluge::project::OwnerParamTarget>(entry.target)) {
			continue; // a knob plays from the table, through the index, which resolves its device itself
		}
		if (std::ranges::find(targets, entry.target) == targets.end()) {
			targets.push_back(entry.target);
		}
	}
	// The device list is the app loop's: its entries are made with the render running, so each projection below finds
	// its cable and only walks and writes the song.
	for (const deluge::project::MidiDeviceRef& device : devices) {
		(void)cable_for(device);
	}
	for (const deluge::project::MappingTarget& target : targets) {
		AudioExcludedSection section; // the projection walks and writes what the song's tick can change
		(void)projectMapping(song, target);
	}
}

std::vector<uint32_t> rowIdsOf(const ::Output* output) {
	std::vector<uint32_t> rows;
	if (output == nullptr || output->type != OutputType::KIT) {
		return rows;
	}
	for (const Drum* drum = static_cast<const Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->id()) {
			rows.push_back(*drum->id());
		}
	}
	return rows;
}

RowCommandMoves::RowCommandMoves(::Song& song) : song_(song) {
	if constexpr (ALPHA_OR_BETA_VERSION) {
		if (&song != currentSong) {
			FREEZE_WITH_ERROR("E469"); // the open count is the current song's: a drain for another song never waits
		}
	}
	applyPendingMappingOps(song_); // the commands as they were include the writes the render held
	for (const Mapping& entry : song_.project.mappings.entries()) {
		if (rowOf(entry.target)) {
			before_.push_back(entry);
		}
	}
	rowCommandMovesOpen++;
}

RowCommandMoves::~RowCommandMoves() {
	rowCommandMovesOpen--;
	applyPendingMappingOps(song_);
}

void recordMovedDrumCommands(::Kit& kit, std::span<const Mapping> before, std::span<const uint32_t> fromRows,
                             ::Song& song) {
	// The writes the render held since @p before was taken land after the moves (the rebuild below), so a forget among
	// them is not undone by a moved command.
	for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		if (!drum->id()) {
			continue;
		}
		recordMovedCommand(song.project.mappings, before, deluge::project::DrumMuteTarget{*drum->id()},
		                   drum->muteMIDICommand, fromRows);
		recordMovedCommand(song.project.mappings, before, deluge::project::LaneInputTarget{*drum->id()},
		                   drum->midiInput, fromRows);
	}
	deluge::midi::rebuildMappingIndex(song);
}

void mappingNoLongerRelative(const deluge::project::OwnerParamTarget& target, const MidiCommand& source, ::Song& song) {
	requireAppLoop();
	const std::span<const Mapping> entries = song.project.mappings.entries();
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].target == deluge::project::MappingTarget{target} && entries[i].source == source) {
			song.project.mappings.set_relative(i, false);
		}
	}
	// Run from the MIDI input path, maybe while it walks the index: the rebuild waits for the next message.
	tableEdited(song);
}

void recordLoadedMappings(::Song& song) {
	MappingTable read;
	for (const Mapping& entry : song.project.mappings.entries()) {
		if (std::holds_alternative<deluge::project::SectionLaunchTarget>(entry.target)) {
			read.learn(entry);
		}
	}
	song.project.mappings = std::move(read);
	if (&song == currentSong) {
		deluge::midi::takeoverStore().clear(); // a reload loses its knobs' states
	}
	recordKnobs({ControlsAddress::Kind::SONG, 0}, *song.live.master_channel.effectsOwner()->knobs, song);
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		recordOutputBindings(*output, song);
	}
	for (Clip* clip : AllClips::everywhere(&song)) {
		recordLearned(song, clip->muteMIDICommand, deluge::project::ClipMuteTarget{clip->id()});
	}
}

MidiCommand sectionCommand(const MappingTable& mappings, uint8_t section) {
	const Mapping* entry = mappings.find(deluge::project::SectionLaunchTarget{section});
	return entry != nullptr ? entry->source : MidiCommand{};
}

void setSectionCommand(MappingTable& mappings, uint8_t section, const MidiCommand& command) {
	if (command.contains_something()) {
		mappings.learn(Mapping{.source = command, .target = deluge::project::SectionLaunchTarget{section}});
	}
	else {
		mappings.unlearn(deluge::project::SectionLaunchTarget{section});
	}
}

Error projectMapping(::Song& song, const deluge::project::MappingTarget& target) {
	const MappingTable& mappings = song.project.mappings;
	return std::visit(
	    [&]<typename T>(const T& t) -> Error {
		    if constexpr (std::is_same_v<T, deluge::project::OwnerParamTarget>) {
			    // A knob plays from the table, through the index: there is its owner to find, and the devices its knobs
			    // name to give entries.
			    if (!effectsOwnerAt(song, t.owner)) {
				    return Error::UNSPECIFIED;
			    }
			    makeKnobDevices(mappings, t.owner);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::ClipMuteTarget>) {
			    Clip* clip = clipWithId(song, t.clip_id);
			    if (clip == nullptr) {
				    return Error::UNSPECIFIED;
			    }
			    projectCommand(mappings, target, clip->muteMIDICommand);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::DrumMuteTarget>
		                       || std::is_same_v<T, deluge::project::LaneInputTarget>) {
			    Drum* drum = drumWithId(song, t.drum_id);
			    if (drum == nullptr) {
				    return Error::UNSPECIFIED;
			    }
			    projectCommand(mappings, target,
			                   std::is_same_v<T, deluge::project::DrumMuteTarget> ? drum->muteMIDICommand
			                                                                      : drum->midiInput);
		    }
		    else if constexpr (std::is_same_v<T, deluge::project::TrackInputTarget>) {
			    Output* output = outputWithId(song, t.output_id);
			    // An audio track has no MIDI input.
			    if (output == nullptr || output->type == OutputType::AUDIO) {
				    return Error::UNSPECIFIED;
			    }
			    projectCommand(mappings, target, static_cast<Instrument*>(output)->midiInput);
		    }
		    else {
			    static_assert(std::is_same_v<T, deluge::project::SectionLaunchTarget>);
			    if (t.section >= kMaxNumSections) {
				    return Error::UNSPECIFIED;
			    }
		    }
		    return Error::NONE;
	    },
	    target);
}

bool holdsOutput(::Song& song, const ::Output& output) {
	return output.id() && outputWithId(song, *output.id()) == &output;
}

bool holdsDrum(::Song& song, const ::Drum& drum) {
	return drum.id() && drumWithId(song, *drum.id()) == &drum;
}

std::vector<Mapping> loadedKnobsOf(const deluge::serialization::Value& element, bool isSound) {
	const deluge::plugin::ParamNaming* naming = deluge::plugin::naming_for(
	    codecPlugins().namings, isSound ? deluge::plugin::kDelugeInstrument : deluge::plugin::kGlobalEffectable);
	if (naming == nullptr) {
		return {};
	}
	std::vector<Mapping> knobs = deluge::project::decode_knobs(element, ControlsAddress{}, *naming);
	for (Mapping& knob : knobs) {
		knob.source = command_of(field_of(knob.source));
	}
	return knobs;
}

bool handsOverKnobs(::Song& song, ::Output& output) {
	const deluge::model::KnobCarrier* knobs = knobsOf(output);
	if (knobs == nullptr) {
		return false;
	}
	if (!knobs->loaded.empty()) {
		return true;
	}
	if (!output.id()) {
		return false;
	}
	applyPendingMappingOps(song); // a knob the render's writes bring counts
	return std::ranges::any_of(song.project.mappings.params_of({ControlsAddress::Kind::OUTPUT, *output.id()}),
	                           turnsNamedParam);
}

bool learnedKnobTurns(::Song* song, deluge::model::EffectsOwner owner, std::optional<ControlsAddress> address,
                      const deluge::project::ParamTarget& param) {
	const auto turns = [&](const Mapping& knob) {
		return std::get<deluge::project::OwnerParamTarget>(knob.target).param == param;
	};
	if (std::ranges::any_of(owner.knobs->loaded, turns)) {
		return true;
	}
	if (song == nullptr || !address) {
		return false;
	}
	// The owner may not have joined the song's lists yet: a preset change hands it its knobs in the table first. With
	// no owner at the address, the entries there are another's only when an owner that had the id has left the song and
	// the render's forget of its entries is still held; the next drain drops them, and until then the zeroing at worst
	// keeps a pitch adjust it would have zeroed.
	const std::optional<deluge::model::EffectsOwner> holder = effectsOwnerAt(*song, *address);
	return (!holder || holder->core == owner.core)
	       && std::ranges::any_of(song->project.mappings.params_of(*address), turns);
}

std::optional<std::vector<Mapping>> savedKnobsOf(::Song& song, deluge::model::EffectsOwner owner,
                                                 std::optional<ControlsAddress> address) {
	if (!address) {
		return std::nullopt;
	}
	const std::optional<deluge::model::EffectsOwner> holder = effectsOwnerAt(song, *address);
	if (!holder || holder->core != owner.core) {
		return std::nullopt;
	}
	return song.project.mappings.params_of(*address);
}

} // namespace deluge::bridge
