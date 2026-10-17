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

#include "deluge-project/arrangement.h"
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

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>

namespace deluge::bridge {

namespace params = deluge::modulation::params;
using deluge::project::ControlsAddress;
using deluge::project::Mapping;
using deluge::project::MappingTable;
using deluge::project::MidiCommand;

namespace {

/// Record @p learned as @p target's binding, when it holds something and the table holds nothing for @p target.
void recordLearned(::Song& song, const LearnedMIDI& learned, deluge::project::MappingTarget target) {
	if (learned.containsSomething() && song.project.mappings.find(target) == nullptr) {
		recordLoadedMapping(song, Mapping{.source = command_of(learned), .target = target});
	}
}

/// Record @p knobs' learned knobs as @p owner's, when the table holds none for @p owner.
void recordKnobs(ControlsAddress owner, const ::ModControllableAudio& knobs, bool isSound, ::Song& song) {
	if (song.project.mappings.params_of(owner).empty()) {
		song.project.mappings.set_params(owner, knobsOf(owner, knobs, isSound));
	}
}

/// Record @p drum's bindings: a sound row's knobs, its mute and its input.
void recordDrumBindings(Drum& drum, ::Song& song) {
	if (!drum.id()) {
		return;
	}
	if (drum.type == DrumType::SOUND) {
		recordKnobs({ControlsAddress::Kind::DRUM, *drum.id()}, *static_cast<SoundDrum*>(&drum), true, song);
	}
	recordLearned(song, drum.muteMIDICommand, deluge::project::DrumMuteTarget{*drum.id()});
	recordLearned(song, drum.midiInput, deluge::project::LaneInputTarget{*drum.id()});
}

/// Whether @p output plays any binding of its own: a learned knob, its input, or a kit's row's knob, mute or input. A
/// fresh output plays none, and recording it touches nothing: an auto-overdub's audio track is made on the song's tick.
bool bringsBindings(Output& output) {
	if (output.type == OutputType::SYNTH || output.type == OutputType::KIT || output.type == OutputType::AUDIO) {
		if (output.toModControllable() != nullptr
		    && !static_cast<ModControllableAudio*>(output.toModControllable())->midi_knobs.empty()) {
			return true;
		}
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
		    || (drum->type == DrumType::SOUND && !static_cast<SoundDrum*>(drum)->midi_knobs.empty())) {
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
		recordKnobs(address, *static_cast<SoundInstrument*>(&output), true, song);
		break;
	case OutputType::KIT:
		recordKnobs(address, *static_cast<Kit*>(&output), false, song);
		break;
	case OutputType::AUDIO:
		recordKnobs(address, *static_cast<AudioOutput*>(&output), false, song);
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

/// Whether @p field plays @p source: from the cable @p source's device resolves to (any device when it names none),
/// on the same channel and note or CC. A saved device reference can name a cable without equalling the reference the
/// cable makes (its name's case, its USB ids), so the cable is what is compared.
bool playsAs(const MidiCommand& source, const LearnedMIDI& field) {
	const LearnedMIDI played = learned_of(source);
	return (!source.device || played.cable != nullptr) && sameLearned(played, field);
}

/// Make @p owner's learned knob list its knobs in @p mappings, in table order. A knob the list leaves as it is (one
/// that plays its entry's source on the same param, relative alike) stays whole: its device (which a name lookup could
/// not tell from another of the same name) and its takeover state with it.
void projectKnobs(const MappingTable& mappings, ControlsAddress address, ::ModControllableAudio& owner, bool isSound) {
	const params::Kind unpatchedKind = isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL;
	deluge::fast_vector<MIDIKnob> knobs;
	for (const Mapping& entry : mappings.params_of(address)) {
		const ParamDescriptor target =
		    descriptorOf(std::get<deluge::project::ParamTarget>(entry.target).param, unpatchedKind);
		const auto kept = std::ranges::find_if(owner.midi_knobs, [&](const MIDIKnob& old) {
			return playsAs(entry.source, old.midiInput) && old.relative == entry.relative
			       && old.paramDescriptor == target;
		});
		if (kept != owner.midi_knobs.end()) {
			knobs.push_back(*kept);
			continue;
		}
		MIDIKnob made;
		made.midiInput = field_of(entry.source);
		made.relative = entry.relative;
		made.paramDescriptor = target;
		knobs.push_back(made);
	}
	owner.midi_knobs = std::move(knobs);
}

} // namespace

void recordLoadedMapping(::Song& song, const Mapping& mapping) {
	song.project.mappings.learn(mapping);
}

std::vector<Mapping> knobsOf(ControlsAddress owner, const ::ModControllableAudio& knobs, bool isSound) {
	const params::Kind unpatchedKind = isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL;
	std::vector<Mapping> entries;
	for (const MIDIKnob& knob : knobs.midi_knobs) {
		entries.push_back(
		    Mapping{.source = command_of(knob.midiInput),
		            .target = deluge::project::ParamTarget{owner, knobTargetOf(knob.paramDescriptor, unpatchedKind)},
		            .relative = knob.relative});
	}
	return entries;
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
	if (song.project.mappings.params_of({ControlsAddress::Kind::OUTPUT, from}).empty()) {
		return;
	}
	song.project.mappings.copy_params(from, to);
	(void)projectMapping(song, deluge::project::ParamTarget{{ControlsAddress::Kind::OUTPUT, to}, {}});
}

/// The ids of the outputs (playing or hibernating), kit rows and clips (session or arrangement) a song holds, each
/// sorted.
struct HeldIds {
	std::vector<uint32_t> outputs;
	std::vector<uint32_t> drums;
	std::vector<uint32_t> clips;

	explicit HeldIds(::Song& song) {
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
		std::ranges::sort(outputs);
		std::ranges::sort(drums);
		std::ranges::sort(clips);
	}

	[[nodiscard]] static bool has(const std::vector<uint32_t>& ids, uint32_t id) {
		return std::ranges::binary_search(ids, id);
	}
};

/// Bring @p song's table into line with what the queue lost: drop every entry whose owner the song no longer holds,
/// then record what each output and kit row plays that the table holds nothing for (a clone's knobs, an arrival's
/// bindings), as a load's sweep does. The render is excluded only while the song's lists are walked: it never writes
/// the table, so the entries are matched against the ids the walk found with it going.
void reconcileWithEngine(::Song& song) {
	std::optional<HeldIds> held;
	{
		AudioExcludedSection section; // the song's tick can splice the lists walked
		held.emplace(song);
	}
	MappingTable& mappings = song.project.mappings;
	std::vector<uint32_t> clips;
	std::vector<uint32_t> outputs;
	std::vector<uint32_t> drums;
	for (const Mapping& entry : mappings.entries()) {
		std::visit(
		    [&]<typename T>(const T& t) {
			    if constexpr (std::is_same_v<T, deluge::project::ParamTarget>) {
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
	}
	for (uint32_t id : drums) {
		mappings.forget_drum(id);
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

/// Whether a write to @p song's table is held for the app loop: the render's to the current song. The render writes
/// another song's (the outgoing one it tears down) at once, as nothing else reaches it.
bool heldForAppLoop(const ::Song& song) {
	return &song == currentSong && onRenderTier();
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
	tableEdited(song);
}

void forgetDrumMappings(::Song& song, uint32_t drum) {
	if (holdForAppLoop(song, {.kind = PendingOp::Kind::FORGET_DRUM, .id = drum})) {
		return;
	}
	applyPendingMappingOps(song);
	song.project.mappings.forget_drum(drum);
	tableEdited(song);
}

void applyPendingMappingOps(::Song& song) {
	if (&song != currentSong) {
		return; // the render holds writes for the current song only
	}
	requireAppLoop();
	if (rowCommandMovesOpen > 0) {
		return; // a kit change is moving its rows' commands: the writes wait for it (RowCommandMoves)
	}
	PendingOps& pending = pendingOps();
	if (pending.count.load(std::memory_order_acquire) == 0 && !pending.overflowed.load(std::memory_order_acquire)) {
		return;
	}
	bool overflowed = false;
	{
		// The render writes the queue, and the ops walk and write what the tick can change. An overflow's reconcile,
		// after, excludes it only where it must.
		AudioExcludedSection section;
		const size_t n = pending.count.exchange(0, std::memory_order_acq_rel);
		overflowed = pending.overflowed.exchange(false, std::memory_order_acq_rel);
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
				break;
			case PendingOp::Kind::FORGET_DRUM:
				mappings.forget_drum(op.id);
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
	if (overflowed) {
		// Which writes were lost, and in what order, is not known: the table takes what the song now holds.
		reconcileWithEngine(song);
	}
	deluge::markMappingIndexStale();
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
	// An owner with no knob list (a MIDI or CV track) or no input (an audio track) refuses its half: nothing to play.
	(void)projectMapping(song, deluge::project::ParamTarget{{ControlsAddress::Kind::OUTPUT, output}, {}});
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
		deluge::project::MappingTarget target = entry.target;
		if (auto* knob = std::get_if<deluge::project::ParamTarget>(&target)) {
			knob->param = {}; // an owner's knob list is projected whole
		}
		if (std::ranges::find(targets, target) == targets.end()) {
			targets.push_back(target);
		}
		devices.push_back(*entry.source.device);
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

void knobNoLongerRelative(::ModControllableAudio& owner, const ::MIDIKnob& knob, ::Song& song) {
	// The writes the render held are not applied here: a clone's would project onto the knob list the caller is
	// walking. Clearing the flag lands the same before or after a held forget or arrival, and a held clone of the
	// owner copies the flag as this message showed it to be.
	requireAppLoop();
	const std::optional<ControlsAddress> address = controlsAddressOf(owner, song);
	if (!address) {
		return;
	}
	bool isSound = false;
	if (controlsOwnerAt(song, *address, &isSound) != &owner) {
		return;
	}
	// The entry is the one the knob was projected from: same param, and its source plays as the knob does (a device's
	// saved ids may not be the cable's own).
	const deluge::project::MappingTarget target = deluge::project::ParamTarget{
	    *address,
	    knobTargetOf(knob.paramDescriptor, isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL)};
	const std::span<const Mapping> entries = song.project.mappings.entries();
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].target == target && sameLearned(learned_of(entries[i].source), knob.midiInput)) {
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
	recordKnobs({ControlsAddress::Kind::SONG, 0}, song.live.global_effectable, false, song);
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
		    if constexpr (std::is_same_v<T, deluge::project::ParamTarget>) {
			    bool isSound = false;
			    ::ModControllableAudio* owner = controlsOwnerAt(song, t.owner, &isSound);
			    if (owner == nullptr) {
				    return Error::UNSPECIFIED;
			    }
			    projectKnobs(mappings, t.owner, *owner, isSound);
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

Error projectRelearnedKnob(::Song& song, const deluge::project::ParamTarget& target, const MidiCommand& replaced,
                           const MidiCommand& source) {
	bool isSound = false;
	::ModControllableAudio* owner = controlsOwnerAt(song, target.owner, &isSound);
	if (owner == nullptr) {
		return Error::UNSPECIFIED;
	}
	const ParamDescriptor param =
	    descriptorOf(target.param, isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL);
	const auto knobPlaying = [&](const MidiCommand& command) {
		return std::ranges::find_if(owner->midi_knobs, [&](const MIDIKnob& knob) {
			return knob.paramDescriptor == param && playsAs(command, knob.midiInput);
		});
	};
	const auto old = knobPlaying(replaced);
	const bool found = old != owner->midi_knobs.end();
	const bool saved = found && old->previousPositionSaved;
	const int32_t position = found ? old->previousPosition : 0;
	projectKnobs(song.project.mappings, target.owner, *owner, isSound);
	if (found) {
		const auto now = knobPlaying(source);
		if (now != owner->midi_knobs.end()) {
			now->previousPositionSaved = saved;
			now->previousPosition = position;
		}
	}
	return Error::NONE;
}

bool knobPlays(const ::MIDIKnob& knob, const deluge::project::MappingTarget& target, uint8_t noteOrCC) {
	const auto* param = std::get_if<deluge::project::ParamTarget>(&target);
	if (param == nullptr || knob.midiInput.noteOrCC != noteOrCC) {
		return false;
	}
	// An output's knobs turn a sound's unpatched params or a global effectable's, as the output is a synth or not.
	const ControlsAddress::Kind owner = param->owner.kind;
	return (owner != ControlsAddress::Kind::SONG
	        && knobTargetOf(knob.paramDescriptor, params::Kind::UNPATCHED_SOUND) == param->param)
	       || (owner != ControlsAddress::Kind::DRUM
	           && knobTargetOf(knob.paramDescriptor, params::Kind::UNPATCHED_GLOBAL) == param->param);
}

bool holdsOutput(::Song& song, const ::Output& output) {
	return output.id() && outputWithId(song, *output.id()) == &output;
}

bool holdsDrum(::Song& song, const ::Drum& drum) {
	return drum.id() && drumWithId(song, *drum.id()) == &drum;
}

std::optional<std::vector<Mapping>> savedKnobsOf(::Song& song, const ::ModControllableAudio& owner,
                                                 std::optional<ControlsAddress> address) {
	bool isSound = false;
	if (!address || controlsOwnerAt(song, *address, &isSound) != &owner) {
		return std::nullopt;
	}
	return song.project.mappings.params_of(*address);
}

} // namespace deluge::bridge
