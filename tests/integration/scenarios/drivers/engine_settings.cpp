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

// engine_settings <song|preset> <SONG|SYNTH|KIT> — a sound's settings through its slot. Every output's and sound
// drum's sound in a loaded song or preset is bound to its slot; so is a synth added to a running song and one that
// replaces another. Every legal value of every setting path an Edit maps is set on every bound sound through
// SetEngineSetting: after each, the slot holds the value, the slot's instrument entries are the encoding of the
// sound's state (equation 1), and the sound's snapshot is the one its state projects (equation 2); then the old value
// is restored. A kit's affect-entire leg sets one path on all its sound drums at once. Exit: 0 every check holds; 1 the
// file does not load; 2 usage/IO; 3 a check fails (printed).

#include "deluge-project/instrument_slot.h"
#include "engine-api/types/sync.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "extern.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/edit.h"
#include "model/edits/set_engine_setting.h"
#include "model/edits/set_source_ranges.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/song_serialization.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "scenarios/slot_coherence.h"
#include "serialization/tree.h"
#include "storage/document_format.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"
#include "storage/value_tree_deserializer.h"

#include <array>
#include <charconv>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using deluge::serialization::Value;

using deluge::scenarios::driver::Io;

/// Where this driver's prints go.
Io io{};

/// A song for a file to be loaded into.
Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	return song;
}

/// The song in @p bytes (of @p format), loaded as the app loads a song; nullptr on a load failure.
Song* loadSong(std::string_view bytes, deluge::storage::DocumentFormat format) {
	Song* song = freshSong();
	std::optional<deluge::storage::Value> tree = parseDocument(bytes, format);
	if (!tree) {
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
	activeReader = &adapter;
	Error e = openTreeFile(adapter, "song", format);
	if (e == Error::NONE) {
		e = deluge::bridge::loadSong(*song, adapter);
	}
	activeReader = &smDeserializer;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	return song;
}

/// The instrument the preset in @p bytes holds, read into @p song as the app reads a preset (not yet in its outputs).
Instrument* loadPreset(std::string_view bytes, OutputType type, Song& song) {
	char const* firstTag = nullptr;
	char const* altTag = nullptr;
	StorageManager::instrumentDocumentTags(type, &firstTag, &altTag);
	deluge::storage::TreeDocument document;
	if (document.open(std::span{reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, /*isJson=*/false,
	                  firstTag, altTag)
	    != Error::NONE) {
		return nullptr;
	}
	Instrument* instrument = nullptr;
	std::string name = "PRESET";
	std::string dir = type == OutputType::SYNTH ? "SYNTHS" : "KITS";
	const Error e =
	    StorageManager::parseInstrumentDocument(document, &song, nullptr, type, false, &instrument, &name, &dir);
	document.release();
	return e == Error::NONE ? instrument : nullptr;
}

/// Every sound of @p output: a synth's own, or a kit's sound drums.
void forEachSound(Output& output, const std::function<void(Sound&, const char*)>& visit) {
	if (output.type == OutputType::SYNTH) {
		visit(*static_cast<SoundInstrument*>(&output), "synth");
	}
	else if (output.type == OutputType::KIT) {
		for (Drum* drum = static_cast<Kit*>(&output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND) {
				visit(*static_cast<SoundDrum*>(drum), "drum");
			}
		}
	}
}

/// 0 when every sound of @p output is bound to its slot, else 3 after printing the first that is not.
int expectBound(Output& output, const char* how) {
	int code = 0;
	if (output.docOutput_ == nullptr) {
		std::fprintf(io.err, "UNBOUND %s: output %s has no record\n", how, output.name.c_str());
		return 3;
	}
	forEachSound(output, [&](Sound& sound, const char* kind) {
		const bool slotted = kind == std::string_view{"synth"}
		                         ? output.docOutput_->instrument.has_value()
		                         : (static_cast<SoundDrum&>(sound).docDrum_ != nullptr
		                            && static_cast<SoundDrum&>(sound).docDrum_->instrument.has_value());
		if (code == 0 && (sound.boundState() == nullptr || !slotted)) {
			std::fprintf(io.err, "UNBOUND %s: a %s of %s has no %s\n", how, kind, output.name.c_str(),
			             sound.boundState() == nullptr ? "bound state" : "slot");
			code = 3;
		}
	});
	return code;
}

/// The setting paths an Edit maps so far.
constexpr std::array<std::string_view, 50> kEditedPaths{
    "polyphonic",
    "voicePriority",
    "maxVoices",
    "mode",
    "transpose",
    "cents",
    "sideChainSend",
    "osc1/type",
    "osc1/transpose",
    "osc1/cents",
    "osc1/retrigPhase",
    "osc1/loopMode",
    "osc1/reversed",
    "osc1/timeStretchEnable",
    "osc1/timeStretchAmount",
    "osc1/linearInterpolation",
    "osc2/type",
    "osc2/transpose",
    "osc2/cents",
    "osc2/retrigPhase",
    "osc2/loopMode",
    "osc2/reversed",
    "osc2/timeStretchEnable",
    "osc2/timeStretchAmount",
    "osc2/linearInterpolation",
    "osc2/oscillatorSync",
    "lfo1/type",
    "lfo1/syncLevel",
    "lfo1/syncType",
    "lfo2/type",
    "lfo2/syncLevel",
    "lfo2/syncType",
    "lfo3/type",
    "lfo3/syncLevel",
    "lfo3/syncType",
    "lfo4/type",
    "lfo4/syncLevel",
    "lfo4/syncType",
    "unison/num",
    "unison/detune",
    "unison/spread",
    "modulator1/transpose",
    "modulator1/cents",
    "modulator1/retrigPhase",
    "modulator2/transpose",
    "modulator2/cents",
    "modulator2/retrigPhase",
    "modulator2/toModulator1",
    "midiOutput/channel",
    "midiOutput/noteForDrum",
};

/// Whether @p path is a sample-playback setting, which only a sample source of a non-FM sound takes.
bool isSamplePlayback(std::string_view path) {
	for (std::string_view name :
	     {"/loopMode", "/reversed", "/timeStretchEnable", "/timeStretchAmount", "/linearInterpolation"}) {
		if (path.ends_with(name)) {
			return true;
		}
	}
	return false;
}

/// Whether @p sound's source that @p path names (`oscN/…`) takes the sample-playback setting: a sample source of a
/// non-FM sound, or for interpolation a live-input one too.
bool playsSamples(Sound& sound, std::string_view path) {
	const OscType type = sound.sources[path[3] - '1'].oscType;
	const bool input = type == OscType::INPUT_L || type == OscType::INPUT_R || type == OscType::INPUT_STEREO;
	return sound.getSynthMode() != SynthMode::FM
	       && (type == OscType::SAMPLE || (input && path.ends_with("/linearInterpolation")));
}

/// The node at @p path (slash-separated) in @p tree, or nullptr.
const Value* leafAt(const Value& tree, std::string_view path) {
	const Value* at = &tree;
	while (at != nullptr && !path.empty()) {
		const size_t slash = path.find('/');
		at = at->isMap() ? at->find(path.substr(0, slash)) : nullptr;
		path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
	}
	return at;
}

/// The legal values of @p row worth setting, given the slot's @p current text: an integer row's ends and the value
/// after the current one, a choice row's every name, a bool's both.
std::vector<Value> legalValues(const deluge::engine::SettingInfo& row, const std::string* current) {
	using Kind = deluge::engine::SettingKind;
	std::vector<Value> values;
	switch (row.kind) {
	case Kind::INTEGER: {
		values.push_back(Value::makeNumber(row.min));
		values.push_back(Value::makeNumber(row.max));
		int64_t now = 0;
		if (current != nullptr
		    && std::from_chars(current->data(), current->data() + current->size(), now).ec == std::errc{}
		    && now + 1 >= row.min && now + 1 <= row.max) {
			values.push_back(Value::makeNumber(now + 1));
		}
		break;
	}
	case Kind::CHOICE:
		for (std::string_view name : row.choices) {
			values.push_back(Value::makeScalar(std::string(name)));
		}
		break;
	case Kind::BOOL:
		values.push_back(Value::makeNumber(0));
		values.push_back(Value::makeNumber(1));
		break;
	case Kind::STRUCTURED:
		break;
	}
	return values;
}

/// @p value as the slot holds it: an LFO's sync level relative to the song's tick magnitude, as a file writes it;
/// everything else as the Edit takes it.
Value slotForm(std::string_view path, const Value& value, Song& song) {
	if (!path.ends_with("/syncLevel")) {
		return value;
	}
	const int32_t level = deluge::serialization::int_of(&value).value_or(0);
	return Value::makeNumber(deluge::engine::sync_level_to_file(level, song.getInputTickMagnitude()));
}

/// The slot's @p held value as an Edit takes it: the inverse of slotForm.
Value editForm(std::string_view path, const Value& held, Song& song) {
	if (!path.ends_with("/syncLevel")) {
		return held;
	}
	const int32_t file = deluge::serialization::int_of(&held).value_or(0);
	return Value::makeNumber(deluge::engine::sync_level_from_file(file, song.getInputTickMagnitude()));
}

/// Empty when @p sound's slot agrees with its state after setting @p path to @p value, else which check fails.
std::string_view coherence(Sound& sound, Song& song, std::string_view path, const Value& value) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	deluge::project::InstrumentSlot* slot = address ? song.project.resolveSlot(*address) : nullptr;
	if (slot == nullptr) {
		return "no slot";
	}
	const Value encoded =
	    deluge::instrument::encode(*sound.boundState(), {.input_tick_magnitude = song.getInputTickMagnitude()});
	const std::string* held = deluge::serialization::text_of(leafAt(slot->state, path));
	if (leafAt(encoded, path) != nullptr && (held == nullptr || *held != slotForm(path, value, song).str())) {
		return "value";
	}
	if (const std::string what = deluge::harness::soundIncoherence(sound, song); !what.empty()) {
		std::fprintf(io.err, "%s\n", what.c_str());
		if (what.starts_with("runtime")) {
			return "equation 0";
		}
		if (what.starts_with("slot")) {
			return "equation 1";
		}
		if (what.starts_with("decode")) {
			return "equation 4";
		}
		return what.starts_with("controls") ? "controls" : "equation 2";
	}
	return {};
}

/// Whether @p sound takes a setting at @p path at all: sample playback only on a source that uses it; an oscillator's
/// own transpose and cents only on a source that plays no samples (a sample source's tuning is its ranges'); an
/// oscillator's type and modulators each only where an FM file holds them; everything else always.
bool takes(Sound& sound, std::string_view path) {
	if (isSamplePlayback(path)) {
		return playsSamples(sound, path);
	}
	if (path.starts_with("osc") && path.ends_with("/type")) {
		return sound.getSynthMode() != SynthMode::FM;
	}
	if (path.starts_with("osc") && (path.ends_with("/transpose") || path.ends_with("/cents"))) {
		return sound.getSynthMode() == SynthMode::FM || sound.sources[path[3] - '1'].oscType != OscType::SAMPLE;
	}
	if (path.starts_with("modulator")) {
		return sound.getSynthMode() == SynthMode::FM;
	}
	return true;
}

/// 0 when every legal value of every edited path, set on @p sound through SetEngineSetting and then undone, leaves its
/// slot coherent; else 3 after printing the first that does not.
int settingLegs(Sound& sound, Song& song, const char* name) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	if (!address) {
		return 0;
	}
	deluge::edits::Context ctx{song, song.project};
	for (std::string_view path : kEditedPaths) {
		const deluge::engine::SettingInfo* row = deluge::engine::find_setting(deluge::instrument::kSettings, path);
		if (!takes(sound, path)) {
			deluge::edits::SetEngineSetting refused{*address, std::string(path), Value::makeNumber(row->min)};
			if (deluge::edits::apply(ctx, refused) == Error::NONE) {
				std::fprintf(io.err, "%s %.*s: taken by a sound or source that does not use it\n", name,
				             static_cast<int>(path.size()), path.data());
				return 3;
			}
			continue;
		}
		const Value* held = leafAt(song.project.resolveSlot(*address)->state, path);
		const std::optional<Value> old =
		    held != nullptr ? std::optional<Value>{editForm(path, *held, song)} : std::nullopt;
		for (const Value& value : legalValues(*row, old ? &old->str() : nullptr)) {
			const Value slotBefore = song.project.resolveSlot(*address)->state;
			deluge::edits::SetEngineSetting edit{*address, std::string(path), value};
			std::string_view failed =
			    deluge::edits::apply(ctx, edit) != Error::NONE ? "refused" : coherence(sound, song, path, value);
			if (failed.empty() && old && deluge::instrument::check_setting(path, *old)) {
				deluge::edits::SetEngineSetting restore{*address, std::string(path), *old};
				failed = deluge::edits::apply(ctx, restore) != Error::NONE ? "restore refused"
				                                                           : coherence(sound, song, path, *old);
				// A subtractive sound's oscillator set to another type and back is the oscillator it was, ranges
				// included — except between sample and wavetable, whose ranges are of different kinds: the runtime
				// keeps only each range's top note across that change, as the menu always has.
				const bool rangeKindChanges = (old->str() == "sample" && value.str() == "wavetable")
				                              || (old->str() == "wavetable" && value.str() == "sample");
				if (failed.empty() && path.ends_with("/type") && sound.getSynthMode() == SynthMode::SUBTRACTIVE
				    && !rangeKindChanges) {
					const Value& now = song.project.resolveSlot(*address)->state;
					if (std::string d = deluge::serialization::first_difference(now, slotBefore); !d.empty()) {
						std::fprintf(io.err, "type round trip differs at %s\n", d.c_str());
						failed = "type round trip";
					}
				}
			}
			if (!failed.empty()) {
				std::fprintf(io.err, "%s %.*s=%s: %.*s\n", name, static_cast<int>(path.size()), path.data(),
				             value.str().c_str(), static_cast<int>(failed.size()), failed.data());
				return 3;
			}
		}
	}
	return 0;
}

/// Whether @p a and @p b are the same oscillator in what no FM file holds: its type, ranges, DX patch and playback.
bool sameHiddenSource(const deluge::instrument::SourceState& a, const deluge::instrument::SourceState& b) {
	return a.osc_type == b.osc_type && a.ranges == b.ranges && a.dx_patch == b.dx_patch
	       && a.repeat_mode == b.repeat_mode && a.reversed == b.reversed
	       && a.pitch_and_speed_independent == b.pitch_and_speed_independent
	       && a.time_stretch_amount == b.time_stretch_amount && a.interpolation_mode == b.interpolation_mode;
}

/// The mode pair: @p sound set to subtractive, FM, then subtractive again, its slot coherent after each, holding the
/// modulators only while FM, and its oscillators afterwards what they were (the types, samples and DX patches no FM
/// file holds); then its old mode back. 0 when each holds, else 3 after printing the first that does not.
int modePairLeg(Sound& sound, Song& song, const char* name) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	if (!address || sound.boundState() == nullptr) {
		return 0;
	}
	const deluge::instrument::SoundState before = *sound.boundState();
	deluge::edits::Context ctx{song, song.project};
	const Value old = *leafAt(song.project.resolveSlot(*address)->state, "mode");
	for (std::string_view mode : {"subtractive", "fm", "subtractive"}) {
		const Value value = Value::makeScalar(std::string(mode));
		deluge::edits::SetEngineSetting edit{*address, "mode", value};
		std::string_view failed =
		    deluge::edits::apply(ctx, edit) != Error::NONE ? "refused" : coherence(sound, song, "mode", value);
		const bool modulators = song.project.resolveSlot(*address)->state.find("modulator1") != nullptr;
		if (failed.empty() && modulators != (mode == "fm")) {
			failed = modulators ? "modulators kept" : "no modulators";
		}
		if (!failed.empty()) {
			std::fprintf(io.err, "%s mode pair %.*s: %.*s\n", name, static_cast<int>(mode.size()), mode.data(),
			             static_cast<int>(failed.size()), failed.data());
			return 3;
		}
		if (mode != "fm") {
			continue;
		}
		// While FM, each oscillator's own tuning moves and comes back: its entry changes, and what no FM file holds of
		// it must outlast that.
		for (std::string path : {"osc1/transpose", "osc2/transpose"}) {
			const int32_t was = static_cast<int32_t>(sound.boundState()->sources[path[3] - '1'].transpose);
			for (int32_t to : {was < 96 ? was + 1 : was - 1, was}) {
				deluge::edits::SetEngineSetting tune{*address, path, Value::makeNumber(to)};
				if (deluge::edits::apply(ctx, tune) != Error::NONE) {
					std::fprintf(io.err, "%s mode pair: %s=%d refused while FM\n", name, path.c_str(), to);
					return 3;
				}
			}
		}
	}
	for (size_t s = 0; s < before.sources.size(); s++) {
		if (!sameHiddenSource(sound.boundState()->sources[s], before.sources[s])) {
			std::fprintf(io.err, "%s mode pair: osc%zu is not the oscillator it was\n", name, s + 1);
			return 3;
		}
	}
	deluge::edits::SetEngineSetting restore{*address, "mode", old};
	return deluge::edits::apply(ctx, restore) == Error::NONE ? 0 : 3;
}

/// The ring-mod leg: a sound outside FM whose first oscillator plays samples is set to ring mod, which only reading a
/// file clamps to a wavetable, and that oscillator reversed and back; it still plays its samples. Then its old mode
/// back. 0 when each holds (or the sound has no such oscillator), else 3 after printing the first that does not.
int ringModLeg(Sound& sound, Song& song, const char* name) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	if (!address || sound.boundState() == nullptr || sound.getSynthMode() == SynthMode::FM
	    || sound.sources[0].oscType != OscType::SAMPLE) {
		return 0;
	}
	const std::vector<deluge::instrument::RangeState> ranges = sound.boundState()->sources[0].ranges;
	deluge::edits::Context ctx{song, song.project};
	const Value old = *leafAt(song.project.resolveSlot(*address)->state, "mode");
	deluge::edits::SetEngineSetting ringMod{*address, "mode", Value::makeScalar("ringmod")};
	std::string_view failed = deluge::edits::apply(ctx, ringMod) != Error::NONE
	                              ? "refused"
	                              : coherence(sound, song, "mode", Value::makeScalar("ringmod"));
	const int32_t reversed = sound.boundState()->sources[0].reversed ? 1 : 0;
	for (int32_t to : {1 - reversed, reversed}) {
		deluge::edits::SetEngineSetting flip{*address, "osc1/reversed", Value::makeNumber(to)};
		if (failed.empty() && deluge::edits::apply(ctx, flip) != Error::NONE) {
			failed = "osc1/reversed refused";
		}
	}
	if (failed.empty()
	    && (sound.sources[0].oscType != OscType::SAMPLE || sound.boundState()->sources[0].ranges != ranges)) {
		failed = "osc1 no longer plays its samples";
	}
	if (!failed.empty()) {
		std::fprintf(io.err, "%s ring mod: %.*s\n", name, static_cast<int>(failed.size()), failed.data());
		return 3;
	}
	deluge::edits::SetEngineSetting restore{*address, "mode", old};
	return deluge::edits::apply(ctx, restore) == Error::NONE ? 0 : 3;
}

/// @p ranges with each range's tuning nudged and its zone's start moved on by one sample where the zone has room: an
/// in-place change of every range.
std::vector<deluge::instrument::RangeState> nudged(std::vector<deluge::instrument::RangeState> ranges) {
	for (deluge::instrument::RangeState& range : ranges) {
		range.transpose = static_cast<int16_t>(range.transpose == 0 ? 1 : 0);
		if (range.start_pos + 1 < range.end_pos) {
			range.start_pos++;
		}
	}
	return ranges;
}

/// The range leg: every sample source of @p sound has its ranges nudged in place through SetSourceRanges, the sound
/// checked, then the old ranges set back. 0 when each holds, else 3 after printing the first that does not.
int rangeLeg(Sound& sound, Song& song, const char* name) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	if (!address) {
		return 0;
	}
	deluge::edits::Context ctx{song, song.project};
	for (int32_t s = 0; s < kNumSources; s++) {
		if (!playsSamples(sound, s == 0 ? "osc1/loopMode" : "osc2/loopMode") || sound.sources[s].ranges.empty()) {
			continue;
		}
		const std::vector<deluge::instrument::RangeState> old = deluge::bridge::rangesOf(sound.sources[s]);
		for (const auto& ranges : {nudged(old), old}) {
			deluge::edits::SetSourceRanges edit{*address, static_cast<uint8_t>(s), ranges};
			const std::string failed = deluge::edits::apply(ctx, edit) != Error::NONE
			                               ? "refused"
			                               : deluge::harness::soundIncoherence(sound, song);
			if (!failed.empty()) {
				std::fprintf(io.err, "%s osc%d ranges: %s\n", name, static_cast<int>(s + 1), failed.c_str());
				return 3;
			}
		}
	}
	return 0;
}

/// The kit-wide range leg: range 0 of every sample drum's first source nudged, one Edit each, as the sample transpose
/// menu does with affect-entire held, then every drum checked. 0 when each holds, else 3 after printing the first that
/// does not.
int affectEntireRangeLeg(Kit& kit, Song& song) {
	deluge::edits::Context ctx{song, song.project};
	std::vector<SoundDrum*> nudgedDrums;
	std::vector<std::vector<deluge::instrument::RangeState>> before;
	for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		auto* soundDrum = drum->type == DrumType::SOUND ? static_cast<SoundDrum*>(drum) : nullptr;
		const auto address = soundDrum != nullptr ? deluge::bridge::slotAddressOf(*soundDrum) : std::nullopt;
		if (!address || !playsSamples(*soundDrum, "osc1/loopMode") || soundDrum->sources[0].ranges.empty()) {
			continue;
		}
		before.push_back(deluge::bridge::rangesOf(soundDrum->sources[0]));
		deluge::edits::SetSourceRanges edit{*address, 0, nudged(before.back())};
		if (deluge::edits::apply(ctx, edit) != Error::NONE) {
			std::fprintf(io.err, "kit %s affect-entire ranges on %s: refused\n", kit.name.c_str(),
			             soundDrum->name().c_str());
			return 3;
		}
		nudgedDrums.push_back(soundDrum);
	}
	for (SoundDrum* drum : nudgedDrums) {
		if (const std::string failed = deluge::harness::soundIncoherence(*drum, song); !failed.empty()) {
			std::fprintf(io.err, "kit %s affect-entire ranges on %s: %s\n", kit.name.c_str(), drum->name().c_str(),
			             failed.c_str());
			return 3;
		}
	}
	for (size_t i = 0; i < nudgedDrums.size(); i++) {
		deluge::edits::SetSourceRanges back{*deluge::bridge::slotAddressOf(*nudgedDrums[i]), 0, before[i]};
		(void)deluge::edits::apply(ctx, back);
	}
	return 0;
}

/// The affect-entire leg: @p path set to @p value on every sound drum of @p kit, one Edit each, as the menus do with
/// affect-entire held. A drum that takes the setting holds it coherently afterwards; one that does not refuses it and
/// keeps its slot. 0 when each holds, else 3 after printing the first that does not.
int affectEntireLeg(Kit& kit, Song& song, std::string_view path, const Value& value) {
	deluge::edits::Context ctx{song, song.project};
	std::vector<SoundDrum*> taking;
	for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		auto* soundDrum = drum->type == DrumType::SOUND ? static_cast<SoundDrum*>(drum) : nullptr;
		const auto address = soundDrum != nullptr ? deluge::bridge::slotAddressOf(*soundDrum) : std::nullopt;
		if (!address) {
			continue;
		}
		const bool shouldTake = takes(*soundDrum, path);
		const Value before = song.project.resolveSlot(*address)->state;
		deluge::edits::SetEngineSetting edit{*address, std::string(path), value};
		const bool took = deluge::edits::apply(ctx, edit) == Error::NONE;
		std::string_view failed;
		if (took != shouldTake) {
			failed = took ? "taken by a drum that does not use it" : "refused";
		}
		else if (!took
		         && !deluge::serialization::first_difference(song.project.resolveSlot(*address)->state, before)
		                 .empty()) {
			failed = "a refusal changed the slot";
		}
		if (!failed.empty()) {
			std::fprintf(io.err, "kit %s affect-entire %.*s=%s on %s: %.*s\n", kit.name.c_str(),
			             static_cast<int>(path.size()), path.data(), value.str().c_str(), soundDrum->name().c_str(),
			             static_cast<int>(failed.size()), failed.data());
			return 3;
		}
		if (took) {
			taking.push_back(soundDrum);
		}
	}
	for (SoundDrum* drum : taking) {
		const std::string_view failed = coherence(*drum, song, path, value);
		if (!failed.empty()) {
			std::fprintf(io.err, "kit %s affect-entire %.*s=%s on %s: %.*s\n", kit.name.c_str(),
			             static_cast<int>(path.size()), path.data(), value.str().c_str(), drum->name().c_str(),
			             static_cast<int>(failed.size()), failed.data());
			return 3;
		}
	}
	return 0;
}

/// Every setting leg on every sound of @p output, and the affect-entire leg on a kit.
int settingsOf(Output& output, Song& song) {
	int code = 0;
	forEachSound(output, [&](Sound& sound, const char* kind) {
		if (code == 0) {
			std::string name = std::string(kind) + " of " + output.name.c_str();
			code = settingLegs(sound, song, name.c_str());
			if (code == 0) {
				code = rangeLeg(sound, song, name.c_str());
			}
			if (code == 0) {
				code = modePairLeg(sound, song, name.c_str());
			}
			if (code == 0) {
				code = ringModLeg(sound, song, name.c_str());
			}
		}
	});
	if (code == 0 && output.type == OutputType::KIT) {
		if (code == 0) {
			code = affectEntireRangeLeg(static_cast<Kit&>(output), song);
		}
		// A kit of sample drums is made mixed: its first sound drum's oscillator becomes a saw, which plays no samples.
		for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type != DrumType::SOUND) {
				continue;
			}
			if (const auto address = deluge::bridge::slotAddressOf(*static_cast<SoundDrum*>(drum))) {
				deluge::edits::Context ctx{song, song.project};
				deluge::edits::SetEngineSetting saw{*address, "osc1/type", Value::makeScalar("saw")};
				(void)deluge::edits::apply(ctx, saw);
			}
			break;
		}
		// Plain, with side effects (voices re-tuned, the FM switch), and refused by the drums that do not use it.
		const std::array<std::pair<std::string_view, Value>, 6> legs{{
		    {"polyphonic", Value::makeScalar("mono")},
		    {"osc1/reversed", Value::makeNumber(1)},
		    {"unison/num", Value::makeNumber(4)},
		    {"osc1/transpose", Value::makeNumber(7)},
		    {"mode", Value::makeScalar("fm")},
		    {"modulator1/transpose", Value::makeNumber(5)},
		}};
		for (const auto& [path, value] : legs) {
			if (code == 0) {
				code = affectEntireLeg(static_cast<Kit&>(output), song, path, value);
			}
		}
	}
	return code;
}

/// The live-unison leg: a note sounds on the first synth of @p song with a clip, and its unison count changes under it
/// through an Edit; the voice keeps sounding and the slot stays coherent. 0 when each holds (or no synth sounds), else
/// 3 after printing the first that does not.
int liveUnisonLeg(Song& song) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		auto* clip = static_cast<InstrumentClip*>(output->getActiveClip());
		if (output->type != OutputType::SYNTH || clip == nullptr) {
			continue;
		}
		auto* synth = static_cast<SoundInstrument*>(output);
		const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(*synth);
		if (!address) {
			continue;
		}
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* ms =
		    setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, synth, clip, &clip->paramManager);
		synth->Sound::noteOn(ms, &synth->arpeggiator, 60, zeroMPEValues);
		const size_t sounding = synth->numActiveVoices();
		if (sounding == 0) {
			continue; // a source with nothing to play: try the next synth
		}
		deluge::edits::Context ctx{song, song.project};
		for (int32_t num : {4, 1, 8}) {
			const Value value = Value::makeNumber(num);
			deluge::edits::SetEngineSetting edit{*address, "unison/num", value};
			std::string_view failed = deluge::edits::apply(ctx, edit) != Error::NONE
			                              ? "refused"
			                              : coherence(*synth, song, "unison/num", value);
			if (failed.empty() && synth->numActiveVoices() != sounding) {
				failed = "the sounding note stopped";
			}
			if (!failed.empty()) {
				std::fprintf(io.err, "synth %s live unison/num=%d: %.*s\n", synth->name.c_str(), num,
				             static_cast<int>(failed.size()), failed.data());
				return 3;
			}
		}
		synth->killAllVoices();
		std::fprintf(io.err, "live unison: %zu voices of %s re-tuned\n", sounding, synth->name.c_str());
		return 0;
	}
	return 0;
}

/// The live-range leg: a note sounds on the first synth of @p song with a clip and a source with ranges, and a range
/// of that source changes in place through an Edit; the voice keeps sounding and the sound stays coherent. 0 when each
/// holds (or no such synth sounds), else 3 after printing the first that does not.
int liveRangeLeg(Song& song) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		auto* clip = static_cast<InstrumentClip*>(output->getActiveClip());
		if (output->type != OutputType::SYNTH || clip == nullptr) {
			continue;
		}
		auto* synth = static_cast<SoundInstrument*>(output);
		const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(*synth);
		int32_t s = 0;
		while (s < kNumSources && synth->sources[s].ranges.empty()) {
			s++;
		}
		if (!address || s == kNumSources) {
			continue;
		}
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* ms =
		    setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, synth, clip, &clip->paramManager);
		synth->Sound::noteOn(ms, &synth->arpeggiator, 60, zeroMPEValues);
		const size_t sounding = synth->numActiveVoices();
		if (sounding == 0) {
			continue;
		}
		std::vector<deluge::instrument::RangeState> ranges = deluge::bridge::rangesOf(synth->sources[s]);
		if (synth->sources[s].oscType == OscType::SAMPLE) {
			ranges.back().transpose = static_cast<int16_t>(ranges.back().transpose == 0 ? 1 : 0);
		}
		else {
			ranges.back().top_note = static_cast<int16_t>(ranges.back().top_note - 1);
		}
		deluge::edits::Context ctx{song, song.project};
		deluge::edits::SetSourceRanges edit{*address, static_cast<uint8_t>(s), ranges};
		std::string failed = deluge::edits::apply(ctx, edit) != Error::NONE
		                         ? "refused"
		                         : deluge::harness::soundIncoherence(*synth, song);
		if (failed.empty() && synth->numActiveVoices() != sounding) {
			failed = "the sounding note stopped";
		}
		synth->killAllVoices();
		if (!failed.empty()) {
			std::fprintf(io.err, "synth %s live osc%d ranges: %s\n", synth->name.c_str(), static_cast<int>(s + 1),
			             failed.c_str());
			return 3;
		}
		std::fprintf(io.err, "live ranges: %zu voices of %s kept\n", sounding, synth->name.c_str());
		return 0;
	}
	return 0;
}

/// Every bound sound of @p output: a synth's own, or each sound drum of a kit.
void collectBoundSounds(Output& output, std::vector<Sound*>& sounds) {
	if (output.type == OutputType::SYNTH) {
		sounds.push_back(static_cast<SoundInstrument*>(&output));
	}
	else if (output.type == OutputType::KIT) {
		for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND) {
				sounds.push_back(static_cast<SoundDrum*>(drum));
			}
		}
	}
}

/// The magnitude leg: the song's tick magnitude moves on by one, as a tempo-scale change moves it, and every bound
/// sound takes an Edit that changes nothing; its LFO sync levels are unchanged and it stays coherent, since its slot's
/// sync levels are relative to the magnitude the slot was written at. Then the magnitude goes back. 0 when each holds,
/// else 3 after printing the first that does not.
int magnitudeLeg(Song& song) {
	std::vector<Sound*> sounds;
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		collectBoundSounds(*output, sounds);
	}
	for (Instrument* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = static_cast<Instrument*>(sleeping->next)) {
		collectBoundSounds(*sleeping, sounds);
	}
	song.project.transport.inside_world_tick_magnitude++;
	int result = 0;
	deluge::edits::Context ctx{song, song.project};
	for (Sound* sound : sounds) {
		const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(*sound);
		if (!address || sound->boundState() == nullptr) {
			continue;
		}
		const auto levels = sound->boundState()->lfo_sync_level;
		const Value priority = *leafAt(song.project.resolveSlot(*address)->state, "voicePriority");
		deluge::edits::SetEngineSetting edit{*address, "voicePriority", priority};
		std::string failed = deluge::edits::apply(ctx, edit) != Error::NONE ? "refused" : "";
		if (failed.empty() && sound->boundState()->lfo_sync_level != levels) {
			failed = "the LFO sync levels moved";
		}
		if (failed.empty()) {
			failed = deluge::harness::soundIncoherence(*sound, song);
		}
		if (!failed.empty()) {
			std::fprintf(io.err, "magnitude +1: %s\n", failed.c_str());
			result = 3;
			break;
		}
	}
	song.project.transport.inside_world_tick_magnitude--;
	return result;
}

/// The hibernation leg, for a song with no hibernating sound: a synth added to it and hibernated (as replacing an
/// edited instrument hibernates it) keeps its slot, and its settings' Edits reach it there. 0 when each holds, else 3
/// after printing the first that does not.
int hibernationLeg(Song& song) {
	for (Instrument* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = static_cast<Instrument*>(sleeping->next)) {
		if (sleeping->type == OutputType::SYNTH || sleeping->type == OutputType::KIT) {
			return 0; // the song's own hibernating sound had every leg
		}
	}
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (synth == nullptr) {
		std::fprintf(io.err, "hibernation: no new synth\n");
		return 3;
	}
	// Its params go to the song's backups, as an instrument with no clip keeps them; a setting's side effects reach
	// them.
	song.live.param_backup.backUp(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
	                              &paramManager, true);
	song.addOutput(synth);
	synth->editedByUser = true;
	song.deleteOrHibernateOutput(synth);
	if (song.live.media.first_hibernating_instrument != synth) {
		std::fprintf(io.err, "hibernation: the synth did not hibernate\n");
		return 3;
	}
	if (int r = expectBound(*synth, "hibernated")) {
		return r;
	}
	return settingsOf(*synth, song);
}

/// @p bytes (a song's XML) with the first kit's first two sound drums given one id, as a save made while two drums
/// shared a record writes them; empty when the song has no such kit.
std::string withSharedDrumId(std::string_view bytes) {
	const size_t kit = bytes.find("<kit");
	const size_t sources = kit == std::string_view::npos ? kit : bytes.find("<soundSources", kit);
	if (sources == std::string_view::npos) {
		return {};
	}
	std::string out(bytes);
	size_t at = sources + 1;
	for (int32_t drum = 0; drum < 2; drum++) {
		size_t tag = out.find("<sound", at);
		while (tag != std::string::npos && out.compare(tag, 13, "<soundSources") == 0) {
			tag = out.find("<sound", tag + 1);
		}
		if (tag == std::string::npos) {
			return {};
		}
		const size_t end = out.find('>', tag);
		const size_t id = out.find(" id=\"", tag);
		if (id != std::string::npos && id < end) {
			const size_t value = id + 5;
			out.replace(value, out.find('"', value) - value, "424242");
		}
		else {
			out.insert(tag + 6, " id=\"424242\"");
		}
		at = out.find('>', tag) + 1;
	}
	return out;
}

/// The duplicate-id leg: the song read again with two of a kit's drums sharing an id; every sound drum must have a
/// record of its own. 0 when they do (or the song has no kit of two sound drums), else 3 after printing.
int sharedDrumIdLeg(std::string_view bytes, deluge::storage::DocumentFormat format) {
	if (format != deluge::storage::DocumentFormat::XML) {
		return 0;
	}
	const std::string shared = withSharedDrumId(bytes);
	if (shared.empty()) {
		return 0;
	}
	Song* song = loadSong(shared, format);
	if (song == nullptr) {
		std::fprintf(io.err, "LOADFAIL shared-id song\n");
		return 1;
	}
	for (Output* output = song->live.media.first_output; output != nullptr; output = output->next) {
		if (output->type != OutputType::KIT) {
			continue;
		}
		std::vector<const void*> records;
		for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type != DrumType::SOUND || drum->docDrum_ == nullptr) {
				continue;
			}
			if (std::ranges::find(records, static_cast<const void*>(drum->docDrum_)) != records.end()) {
				std::fprintf(io.err, "SHARED drum record in kit %s\n", output->name.c_str());
				return 3;
			}
			records.push_back(drum->docDrum_);
		}
	}
	return 0;
}

int engine_settings_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 3) {
		std::fprintf(io.err, "usage: %s <file> <SONG|SYNTH|KIT>\n", argv[0]);
		return 2;
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (f == nullptr) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::string bytes;
	char chunk[4096];
	for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
		bytes.append(chunk, n);
	}
	std::fclose(f);
	const std::string_view kind = argv[2];

	if (kind == "SONG") {
		Song* song = loadSong(bytes, deluge::storage::formatOfPath(argv[1]));
		if (song == nullptr) {
			std::fprintf(io.err, "LOADFAIL\n");
			return 1;
		}
		for (Output* output = song->live.media.first_output; output != nullptr; output = output->next) {
			if (int r = expectBound(*output, "loaded song")) {
				return r;
			}
			if (int r = settingsOf(*output, *song)) {
				return r;
			}
		}
		for (Instrument* sleeping = song->live.media.first_hibernating_instrument; sleeping != nullptr;
		     sleeping = static_cast<Instrument*>(sleeping->next)) {
			if (int r = expectBound(*sleeping, "hibernating")) {
				return r;
			}
			if (int r = settingsOf(*sleeping, *song)) {
				return r;
			}
		}
		if (int r = liveUnisonLeg(*song)) {
			return r;
		}
		if (int r = liveRangeLeg(*song)) {
			return r;
		}
		if (int r = hibernationLeg(*song)) {
			return r;
		}
		if (int r = magnitudeLeg(*song)) {
			return r;
		}
		// Last: it reads the song again, and the copy becomes the current song.
		if (int r = sharedDrumIdLeg(bytes, deluge::storage::formatOfPath(argv[1]))) {
			return r;
		}
		std::fprintf(io.err, "every sound of the song is bound and its settings are coherent\n");
		return 0;
	}

	const OutputType type = kind == "KIT" ? OutputType::KIT : OutputType::SYNTH;
	currentSong = freshSong();
	Instrument* first = loadPreset(bytes, type, *currentSong);
	if (first == nullptr) {
		std::fprintf(io.err, "LOADFAIL\n");
		return 1;
	}
	currentSong->addOutput(first);
	if (int r = expectBound(*first, "added to a running song")) {
		return r;
	}
	Instrument* second = loadPreset(bytes, type, *currentSong);
	if (second == nullptr) {
		std::fprintf(io.err, "LOADFAIL second copy\n");
		return 1;
	}
	currentSong->replaceInstrument(first, second);
	if (int r = expectBound(*second, "replacing another")) {
		return r;
	}
	if (int r = settingsOf(*second, *currentSong)) {
		return r;
	}
	std::fprintf(io.err, "added and replacing sounds are bound and their settings are coherent\n");
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(engine_settings, engine_settings_main)
