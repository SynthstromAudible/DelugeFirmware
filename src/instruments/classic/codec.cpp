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

#include "instruments/classic/codec.h"
#include "instruments/classic/flat_layout.h"
#include "instruments/classic/state_access.h"

#include "effects/codec.h"
#include "instruments/classic/dx7_init_voice.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <tuple>
#include <utility>

namespace deluge::instruments::classic {

using serialization::Value;

namespace {

/// The top-level keys encode writes, in file order: the sound's own, and its effects'.
constexpr std::array<std::string_view, 30> kStateKeys{"polyphonic",
                                                      "voicePriority",
                                                      "sideChainSend",
                                                      "mode",
                                                      "transpose",
                                                      "cents",
                                                      "modFXType",
                                                      "lpfMode",
                                                      "hpfMode",
                                                      "filterRoute",
                                                      "clippingAmount",
                                                      "maxVoices",
                                                      "osc1",
                                                      "osc2",
                                                      "lfo1",
                                                      "lfo2",
                                                      "lfo3",
                                                      "lfo4",
                                                      "envelope1Settings",
                                                      "envelope2Settings",
                                                      "envelope3Settings",
                                                      "envelope4Settings",
                                                      "modulator1",
                                                      "modulator2",
                                                      "unison",
                                                      "midiOutput",
                                                      "delay",
                                                      "sidechain",
                                                      "audioCompressor",
                                                      "stutter"};

/// A run of kStateKeys: where it starts and how many keys it holds.
struct KeyRun {
	size_t first;
	size_t count;
};
constexpr KeyRun kHeadKeys{.first = 0, .count = 6};            ///< polyphonic … cents
constexpr KeyRun kEffectAttributeKeys{.first = 6, .count = 5}; ///< modFXType … clippingAmount
constexpr KeyRun kMaxVoicesKeys{.first = 11, .count = 1};      ///< maxVoices
constexpr KeyRun kBodyKeys{.first = 12, .count = 13};          ///< osc1 … unison
constexpr KeyRun kMidiOutputKeys{.first = 25, .count = 1};     ///< midiOutput
constexpr KeyRun kDelayKeys{.first = 26, .count = 1};          ///< delay
constexpr KeyRun kEffectTagKeys{.first = 27, .count = 3};      ///< sidechain, audioCompressor, stutter
static_assert(kEffectTagKeys.first + kEffectTagKeys.count == kStateKeys.size());

/// The keys of kStateKeys whose entries are the host's, the sound's routing (`project::encode_routing` writes them; the
/// instrument library does not depend on the model's): the instrument holds none of them, only their place in the file.
constexpr std::array<std::string_view, 2> kHostKeys{"sideChainSend", "midiOutput"};

/// Whether @p key is one of the host's (kHostKeys).
constexpr bool is_host_key(std::string_view key) {
	return std::ranges::contains(kHostKeys, key);
}
static_assert(std::ranges::count_if(kStateKeys, is_host_key) == std::ssize(kHostKeys), "every host key is a state key");

/// The keys of kStateKeys the instrument writes, in file order: all but the host's.
constexpr auto kInstrumentKeys = [] {
	std::array<std::string_view, kStateKeys.size() - kHostKeys.size()> keys{};
	std::ranges::copy_if(kStateKeys, keys.begin(), std::not_fn(is_host_key));
	return keys;
}();

/// The top-level key of older files decode still reads and encode never writes: the sidechain by its old name, as the
/// factory presets and official firmware write it.
constexpr std::array<std::string_view, 1> kOldStateKeys{"compressor"};

/// A number as a file holds it: a 32-bit signed integer, whatever the field's own type.
Value number(int64_t value) {
	return Value::makeNumber(static_cast<int32_t>(value));
}

Value text(std::string_view value) {
	return Value::makeScalar(std::string(value));
}

/// @p bytes as upper-case hex, two digits a byte.
std::string hex(std::span<const uint8_t> bytes) {
	constexpr std::string_view kDigits = "0123456789ABCDEF";
	std::string out;
	out.reserve(bytes.size() * 2);
	for (uint8_t b : bytes) {
		out.push_back(kDigits[b >> 4]);
		out.push_back(kDigits[b & 15]);
	}
	return out;
}

Value boxed(std::string_view name, Value value) {
	auto box = Value::makeMap();
	box.set(std::string(name), std::move(value));
	return box;
}

void append(Value& to, const Value& from) {
	for (const auto& [key, value] : from.entries()) {
		to.set(key, value);
	}
}

// The components' entries: each writes its own keys.

/// A sample range's file, tuning and zone, into @p map.
void sample_range_entry(Value& map, const synth::SourceRange& range) {
	map.set("fileName", text(range.file_path));
	if (range.tuning.transpose != 0) {
		map.set("transpose", number(range.tuning.transpose));
	}
	if (range.tuning.cents != 0) {
		map.set("cents", number(range.tuning.cents));
	}
	auto zone_map = Value::makeMap();
	zone_map.set("startSamplePos", number(static_cast<int64_t>(range.zone.start)));
	zone_map.set("endSamplePos", number(static_cast<int64_t>(range.zone.end)));
	if (range.zone.loop_start != 0) {
		zone_map.set("startLoopPos", number(range.zone.loop_start));
	}
	if (range.zone.loop_end != 0) {
		zone_map.set("endLoopPos", number(range.zone.loop_end));
	}
	map.set("zone", std::move(zone_map));
}

/// A sample oscillator's entries after its type: how it plays and its ranges.
void sample_entries(Value& map, const synth::SampleState& sample) {
	const synth::SamplePlayback& p = sample.playback;
	map.set("loopMode", number(std::to_underlying(p.repeat_mode)));
	map.set("reversed", number(p.reversed ? 1 : 0));
	map.set("timeStretchEnable", number(p.pitch_and_speed_independent ? 1 : 0));
	map.set("timeStretchAmount", number(p.time_stretch_amount));
	if (p.interpolation == plugin::InterpolationMode::LINEAR) {
		map.set("linearInterpolation", number(1));
	}
	if (sample.ranges.size() == 1) {
		sample_range_entry(map, sample.ranges.front());
	}
	else if (sample.ranges.size() > 1) {
		auto list = Value::makeArray();
		for (size_t e = 0; e < sample.ranges.size(); e++) {
			auto range = Value::makeMap();
			if (e != sample.ranges.size() - 1) {
				range.set("rangeTopNote", number(sample.ranges[e].top_note));
			}
			sample_range_entry(range, sample.ranges[e]);
			list.append(boxed("sampleRange", std::move(range)));
		}
		map.set("sampleRanges", std::move(list));
	}
}

/// An oscillator's pitch entries: its tuning, the sync when it is the second and syncs, its retrigger phase.
void pitch_entries(Value& map, const synth::Pitch& pitch, size_t s, bool sync) {
	map.set("transpose", number(pitch.tuning.transpose));
	map.set("cents", number(pitch.tuning.cents));
	if (s == 1 && sync) {
		map.set("oscillatorSync", number(1));
	}
	map.set("retrigPhase", number(phase_word(pitch.retrigger)));
}

/// A wavetable oscillator's ranges: one file, or each range's top note (but the last's) and file.
void wavetable_entries(Value& map, const synth::WavetableState& wavetable) {
	if (wavetable.ranges.size() == 1) {
		map.set("fileName", text(wavetable.ranges.front().file_path));
	}
	else if (wavetable.ranges.size() > 1) {
		auto list = Value::makeArray();
		for (size_t e = 0; e < wavetable.ranges.size(); e++) {
			auto range = Value::makeMap();
			if (e != wavetable.ranges.size() - 1) {
				range.set("rangeTopNote", number(wavetable.ranges[e].top_note));
			}
			range.set("fileName", text(wavetable.ranges[e].file_path));
			list.append(boxed("wavetableRange", std::move(range)));
		}
		map.set("wavetableRanges", std::move(list));
	}
}

/// A DX7 oscillator's patch: its bytes, and its engine mode and random detune where they are set.
void dx7_entries(Value& map, const synth::DxPatch& patch) {
	map.set("dx7patch", text(hex(patch.params)));
	if (patch.engine_mode != 0) {
		map.set("dx7enginemode", number(patch.engine_mode));
	}
	if (patch.random_detune != 0) {
		map.set("dx7randomdetune", number(patch.random_detune));
	}
}

/// Oscillator @p s of a subtractive or ring-mod sound, by its kind.
Value slot_entry(const synth::SlotState& slot, OscType type, size_t s, bool sync) {
	auto map = Value::makeMap();
	map.set("type", text(to_string(type)));
	if (const auto* sample = std::get_if<synth::SampleState>(&slot)) {
		sample_entries(map, *sample);
		return map;
	}
	if (const auto* va = std::get_if<synth::VaState>(&slot)) {
		pitch_entries(map, va->pitch, s, sync);
	}
	else if (const auto* wavetable = std::get_if<synth::WavetableState>(&slot)) {
		pitch_entries(map, wavetable->pitch, s, sync);
		wavetable_entries(map, *wavetable);
	}
	else if (const auto* dx = std::get_if<synth::Dx7State>(&slot)) {
		pitch_entries(map, dx->pitch, s, sync);
		if (dx->patch) {
			dx7_entries(map, *dx->patch);
		}
	}
	else if (const auto* input = std::get_if<synth::InputState>(&slot)) {
		pitch_entries(map, input->pitch, s, sync);
	}
	return map;
}

/// Oscillator @p s's entry in @p state: an FM carrier's pitch, or the slot's kind.
Value oscillator_entry(const SoundState& state, size_t s) {
	if (const auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		auto map = Value::makeMap();
		pitch_entries(map, fm->carriers[s], s, fm->sync);
		return map;
	}
	const synth::DualSourceState& dual = *dual_of(state.plays);
	return slot_entry(dual.slots[s], source_type(state, static_cast<SourceId>(s)), s, oscillator_sync(state));
}

/// The envelope an `envelope<n>Settings` key names.
std::optional<size_t> envelope_index(std::string_view key) {
	if (key.size() != 17 || !key.starts_with("envelope") || !key.ends_with("Settings") || key[8] < '1'
	    || key[8] > '4') {
		return std::nullopt;
	}
	return static_cast<size_t>(key[8] - '1');
}

/// An envelope's entry as main writes it: nothing for the classic ADSR; otherwise its mode and curve, and its follow
/// input when that is not Z.
std::optional<Value> envelope_entry(const synth::EnvelopeState& e) {
	if (e.is_default()) {
		return std::nullopt;
	}
	auto map = Value::makeMap();
	map.set("mode", text(synth::to_string(e.mode)));
	map.set("curve", number(e.curve));
	if (e.follow != synth::EnvelopeFollow::Z) {
		map.set("follow", text(synth::to_string(e.follow)));
	}
	return map;
}

Value lfo_entry(const synth::LfoState& lfo_state, const plugin::CodecContext& context) {
	auto map = Value::makeMap();
	map.set("type", text(to_string(lfo_state.shape)));
	map.set("syncLevel",
	        number(plugin::sync_level_to_file(plugin::index_of(lfo_state.sync_level), context.input_tick_magnitude)));
	map.set("syncType", number(std::to_underlying(lfo_state.sync_type)));
	return map;
}

Value modulator_entry(const synth::Pitch& pitch, size_t m, bool routing) {
	auto map = Value::makeMap();
	map.set("transpose", number(pitch.tuning.transpose));
	map.set("cents", number(pitch.tuning.cents));
	map.set("retrigPhase", number(phase_word(pitch.retrigger)));
	if (m == 1) {
		map.set("toModulator1", number(routing ? 1 : 0));
	}
	return map;
}

std::optional<Value> scalar_entry(const SoundState& state, std::string_view key) {
	const synth::VoicingState& v = state.plays.voicing;
	if (key == "polyphonic") {
		return text(to_string(v.polyphony));
	}
	if (key == "voicePriority") {
		return number(std::to_underlying(v.priority));
	}
	if (key == "mode") {
		return text(to_string(synth_mode(state)));
	}
	if (key == "transpose") {
		return v.master.transpose != 0 ? std::optional{number(v.master.transpose)} : std::nullopt;
	}
	if (key == "cents") {
		return v.master.cents != 0 ? std::optional{number(v.master.cents)} : std::nullopt;
	}
	if (key == "maxVoices") {
		return number(v.max_voices);
	}
	return std::nullopt;
}

/// Whether slot @p s differs between @p a and @p b in anything its entry writes or keeps.
bool slot_differs(const SoundState& a, const SoundState& b, size_t s) {
	const auto id = static_cast<SourceId>(s);
	const DxPatchState* pa = dx_patch(a, id);
	const DxPatchState* pb = dx_patch(b, id);
	return source_type(a, id) != source_type(b, id) || source_tuning(a, id) != source_tuning(b, id)
	       || sample_playback(a, id) != sample_playback(b, id) || !same_ranges(a, b, id)
	       || (pa != nullptr) != (pb != nullptr) || (pa != nullptr && *pa != *pb)
	       || source_retrigger(a, id) != source_retrigger(b, id)
	       || (s == 1 && oscillator_sync(a) != oscillator_sync(b));
}

} // namespace

std::vector<std::string_view> changed_entries(const SoundState& a, const SoundState& b) {
	const synth::VoicingState va = voicing(a);
	const synth::VoicingState vb = voicing(b);
	const effects::EffectStates& fa = sound_effects(a);
	const effects::EffectStates& fb = sound_effects(b);
	const std::array<synth::Pitch, 2> ma = modulators(a);
	const std::array<synth::Pitch, 2> mb = modulators(b);
	const std::array<bool, kStateKeys.size()> differ{
	    va.polyphony != vb.polyphony,
	    va.priority != vb.priority,
	    false, // sideChainSend: the host's
	    synth_mode(a) != synth_mode(b),
	    va.master.transpose != vb.master.transpose,
	    va.master.cents != vb.master.cents,
	    fa.mod_fx != fb.mod_fx,
	    fa.filter.lpf_mode != fb.filter.lpf_mode,
	    fa.filter.hpf_mode != fb.filter.hpf_mode,
	    fa.filter.route != fb.filter.route,
	    fa.saturation != fb.saturation,
	    va.max_voices != vb.max_voices,
	    slot_differs(a, b, 0),
	    slot_differs(a, b, 1),
	    lfo(a, 0) != lfo(b, 0),
	    lfo(a, 1) != lfo(b, 1),
	    lfo(a, 2) != lfo(b, 2),
	    lfo(a, 3) != lfo(b, 3),
	    envelope(a, 0) != envelope(b, 0),
	    envelope(a, 1) != envelope(b, 1),
	    envelope(a, 2) != envelope(b, 2),
	    envelope(a, 3) != envelope(b, 3),
	    ma[0] != mb[0],
	    ma[1] != mb[1] || modulator1_to_modulator0(a) != modulator1_to_modulator0(b),
	    va.unison != vb.unison,
	    false, // midiOutput: the host's
	    fa.delay != fb.delay,
	    fa.sidechain != fb.sidechain,
	    fa.compressor != fb.compressor,
	    fa.stutter != fb.stutter,
	};
	std::vector<std::string_view> changed;
	for (size_t k = 0; k < kStateKeys.size(); k++) {
		if (differ[k]) {
			changed.push_back(kStateKeys[k]);
		}
	}
	return changed;
}

std::optional<Value> encode_entry(const SoundState& state, std::string_view key, const plugin::CodecContext& context,
                                  std::optional<int32_t> compressor_threshold) {
	if (deluge::effects::is_effect_key(key)) {
		return deluge::effects::encode_entry(state.plays.fx, key, context, compressor_threshold);
	}
	if (std::optional<Value> scalar = scalar_entry(state, key)) {
		return scalar;
	}
	if (key == "osc1" || key == "osc2") {
		return oscillator_entry(state, key == "osc1" ? 0 : 1);
	}
	if (key.size() == 4 && key.starts_with("lfo") && key[3] >= '1' && key[3] <= '4') {
		return lfo_entry(lfo(state, static_cast<size_t>(key[3] - '1')), context);
	}
	if (const std::optional<size_t> e = envelope_index(key)) {
		return envelope_entry(envelope(state, *e));
	}
	if (key == "modulator1" || key == "modulator2") {
		const auto* fm = std::get_if<synth::FmState>(&state.plays.engine);
		if (fm == nullptr) {
			return std::nullopt; // only FM writes its modulators
		}
		const size_t m = key == "modulator1" ? 0 : 1;
		return modulator_entry(fm->modulators[m], m, fm->modulator1_to_modulator0);
	}
	if (key == "unison") {
		const synth::UnisonState& u = state.plays.voicing.unison;
		auto map = Value::makeMap();
		map.set("num", number(u.count));
		map.set("detune", number(u.detune));
		map.set("spread", number(u.spread));
		return map;
	}
	return std::nullopt;
}

SoundPieces encode_pieces(const SoundState& state, const plugin::CodecContext& context,
                          std::optional<int32_t> compressor_threshold, const serialization::Value* host) {
	SoundPieces pieces{.head = Value::makeMap(),
	                   .effect_attributes = Value::makeMap(),
	                   .max_voices = Value::makeMap(),
	                   .body = Value::makeMap(),
	                   .midi_output = Value::makeMap(),
	                   .delay = Value::makeMap(),
	                   .effect_tags = Value::makeMap()};
	const auto put = [&](Value& piece, std::span<const std::string_view> keys) {
		for (const std::string_view key : keys) {
			if (host != nullptr && std::ranges::find(kHostKeys, key) != kHostKeys.end()) {
				if (const Value* entry = host->find(key)) {
					piece.set(std::string(key), *entry);
				}
			}
			else if (std::optional<Value> entry = encode_entry(state, key, context, compressor_threshold)) {
				piece.set(std::string(key), std::move(*entry));
			}
		}
	};
	// kStateKeys is file order; each piece is a run of it
	const std::span<const std::string_view> keys{kStateKeys};
	put(pieces.head, keys.subspan(kHeadKeys.first, kHeadKeys.count));
	put(pieces.effect_attributes, keys.subspan(kEffectAttributeKeys.first, kEffectAttributeKeys.count));
	put(pieces.max_voices, keys.subspan(kMaxVoicesKeys.first, kMaxVoicesKeys.count));
	put(pieces.body, keys.subspan(kBodyKeys.first, kBodyKeys.count));
	put(pieces.midi_output, keys.subspan(kMidiOutputKeys.first, kMidiOutputKeys.count));
	put(pieces.delay, keys.subspan(kDelayKeys.first, kDelayKeys.count));
	put(pieces.effect_tags, keys.subspan(kEffectTagKeys.first, kEffectTagKeys.count));
	return pieces;
}

Value encode(const SoundState& state, const plugin::CodecContext& context) {
	SoundPieces pieces = encode_pieces(state, context, std::nullopt);
	auto map = Value::makeMap();
	append(map, pieces.head);
	append(map, pieces.effect_attributes);
	append(map, pieces.max_voices);
	append(map, pieces.body);
	append(map, pieces.midi_output);
	append(map, pieces.delay);
	append(map, pieces.effect_tags);
	return map;
}

std::span<const std::string_view> state_keys() {
	return kInstrumentKeys;
}

bool is_state_key(std::string_view key) {
	return std::ranges::contains(kInstrumentKeys, key);
}

serialization::Value with_state(const serialization::Value& element, const SoundState& state,
                                const plugin::CodecContext& context) {
	const serialization::Value encoded = encode(state, context);
	serialization::Value out = serialization::Value::makeMap();
	std::vector<std::string_view> placed;
	if (element.isMap()) {
		for (const auto& [key, value] : element.entries()) {
			if (std::ranges::find(kOldStateKeys, key) != kOldStateKeys.end()) {
				continue; // the encoding writes it in its own form, and the old form read after it would undo it
			}
			if (!is_state_key(key)) {
				out.set(key, value);
				continue;
			}
			if (const serialization::Value* now = encoded.find(key)) {
				out.set(key, *now);
				placed.push_back(key);
			}
		}
	}
	for (const auto& [key, value] : encoded.entries()) {
		if (std::ranges::find(placed, std::string_view(key)) == placed.end()) {
			out.set(key, value);
		}
	}
	return out;
}

namespace {

int32_t intOf(const Value& value) {
	return serialization::int_of(&value).value_or(0);
}

/// The most cents a tune can be: ±50, a quarter tone; cents × 42949672 (a cent's phase-increment step) overflows past
/// it.
constexpr int32_t kMaxCents = 50;

/// A cents attribute, held to ±kMaxCents: a file may hold any number.
int8_t cents_of(const Value& value) {
	return static_cast<int8_t>(std::clamp(intOf(value), -kMaxCents, kMaxCents));
}

std::string_view textOf(const Value& value) {
	return value.isScalar() ? std::string_view(value.str()) : std::string_view{};
}

/// What the reader knows of a source beyond its state: whether its ranges are wavetable ranges, and whether two of them
/// on one top note are refused (a file's) or kept in order (a slot's, whose runtime holds them).
struct SourceReading {
	bool wavetableRanges{false};
	bool refuseDuplicates{true};
};

/// A source's ranges become @p wavetable ranges, keeping only their top notes, and there is at least one, as switching
/// a source to a sample or wavetable oscillator does when its ranges were the other kind.
void changeRangeKind(SourceState& src, SourceReading& reading, bool wavetable) {
	if (reading.wavetableRanges == wavetable) {
		return;
	}
	reading.wavetableRanges = wavetable;
	for (RangeState& range : src.ranges) {
		range = RangeState{.top_note = range.top_note};
	}
	if (src.ranges.empty()) {
		src.ranges.push_back(RangeState{.top_note = kOpenTopNote});
	}
}

RangeState& firstRange(SourceState& src) {
	if (src.ranges.empty()) {
		src.ranges.push_back(RangeState{.top_note = kOpenTopNote});
	}
	return src.ranges.front();
}

DxPatchState& dxPatch(SourceState& src) {
	if (!src.dx_patch) {
		src.dx_patch.emplace(DxPatchState{.params = kDx7InitVoice});
	}
	return *src.dx_patch;
}

void setOscType(SourceState& src, SourceReading& reading, OscType type) {
	if (type == OscType::SAMPLE) {
		changeRangeKind(src, reading, false);
	}
	else if (type == OscType::WAVETABLE) {
		changeRangeKind(src, reading, true);
	}
	src.osc_type = type;
	if (type == OscType::DX7) {
		(void)dxPatch(src);
	}
}

SampleRepeatMode repeatModeOf(const Value& value) {
	const int32_t mode = std::clamp(intOf(value), 0, static_cast<int32_t>(SampleRepeatMode::STRETCH));
	return static_cast<SampleRepeatMode>(mode);
}

/// A zone in samples, into @p range.
void readZone(const Value& zone, RangeState& range, bool resetPositions) {
	if (resetPositions) {
		range.start_pos = 0;
		range.end_pos = 0;
	}
	if (!zone.isMap()) {
		return;
	}
	for (const auto& [key, value] : zone.entries()) {
		if (key == "startSamplePos") {
			range.start_pos =
			    static_cast<uint64_t>(static_cast<int64_t>(intOf(value))); // a 32-bit file value, sign-extended
		}
		else if (key == "endSamplePos") {
			range.end_pos = static_cast<uint64_t>(static_cast<int64_t>(intOf(value)));
		}
		else if (key == "startLoopPos") {
			range.loop_start_pos = static_cast<uint32_t>(intOf(value));
		}
		else if (key == "endLoopPos") {
			range.loop_end_pos = static_cast<uint32_t>(intOf(value));
		}
	}
}

std::expected<void, plugin::StateError> readRanges(const Value& list, SourceState& src, bool refuseDuplicates) {
	const bool wavetable = src.osc_type == OscType::WAVETABLE;
	for (const serialization::NamedChild& child : serialization::children(list)) {
		if ((child.name != "sampleRange" && child.name != "wavetableRange") || !child.node->isMap()) {
			continue;
		}
		RangeState range{.top_note = kOpenTopNote};
		for (const auto& [key, value] : child.node->entries()) {
			if (key == "fileName") {
				range.file_path = std::string(textOf(value));
			}
			else if (key == "rangeTopNote") {
				range.top_note = static_cast<int16_t>(intOf(value));
			}
			else if (!wavetable && key == "zone") {
				readZone(value, range, false);
			}
			else if (!wavetable && key == "transpose") {
				range.transpose = static_cast<int16_t>(intOf(value));
			}
			else if (!wavetable && key == "cents") {
				range.cents = cents_of(value);
			}
		}
		auto at = std::ranges::upper_bound(src.ranges, range.top_note, {}, &RangeState::top_note);
		if (refuseDuplicates && at != src.ranges.begin() && std::prev(at)->top_note == range.top_note) {
			return std::unexpected(plugin::StateError::DUPLICATE_RANGE);
		}
		src.ranges.insert(at, std::move(range));
	}
	return {};
}

// An oscillator element read back, one branch per key and per legacy form:
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<void, plugin::StateError> read_source(const Value& osc, FlatSoundState& state, size_t s,
                                                    SourceReading& reading) {
	SourceState& src = state.sources[s];
	if (!osc.isMap()) {
		return {};
	}
	for (const auto& [key, value] : osc.entries()) {
		if (key == "type") {
			setOscType(src, reading, parse_osc_type(textOf(value)));
		}
		else if (key == "transpose") {
			src.transpose = static_cast<int16_t>(intOf(value));
		}
		else if (key == "cents") {
			src.cents = cents_of(value);
		}
		else if (key == "loopMode") {
			src.repeat_mode = repeatModeOf(value);
		}
		else if (key == "oscillatorSync") {
			state.oscillator_sync = intOf(value) != 0;
		}
		else if (key == "reversed") {
			src.reversed = intOf(value) != 0;
		}
		else if (key == "dx7patch") {
			DxPatchState& patch = dxPatch(src);
			const std::string_view hexText = textOf(value);
			for (size_t b = 0; b < patch.params.size() && 2 * b + 1 < hexText.size(); b++) {
				auto nibble = [](char c) -> int {
					return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c - 'a' + 10;
				};
				patch.params[b] = static_cast<uint8_t>((nibble(hexText[2 * b]) << 4) | nibble(hexText[2 * b + 1]));
			}
		}
		else if (key == "dx7randomdetune") {
			dxPatch(src).random_detune = intOf(value);
		}
		else if (key == "dx7enginemode") {
			dxPatch(src).engine_mode = static_cast<uint8_t>(intOf(value));
		}
		else if (key == "timeStretchEnable") {
			src.pitch_and_speed_independent = intOf(value) != 0;
		}
		else if (key == "timeStretchAmount") {
			src.time_stretch_amount = static_cast<int8_t>(intOf(value));
		}
		else if (key == "linearInterpolation") {
			if (intOf(value) != 0) {
				src.interpolation_mode = plugin::InterpolationMode::LINEAR;
			}
		}
		else if (key == "retrigPhase") {
			state.osc_retrigger_phase[s] = static_cast<uint32_t>(intOf(value));
		}
		else if (key == "fileName") {
			firstRange(src).file_path = std::string(textOf(value));
		}
		else if (key == "zone") {
			readZone(value, firstRange(src), true);
		}
		else if (key == "sampleRanges" || key == "wavetableRanges") {
			src.ranges.clear(); // the list is every range the source has, the one switching its type made included
			if (auto read = readRanges(value, src, reading.refuseDuplicates); !read) {
				return read;
			}
		}
	}
	return {};
}

void read_modulator(const Value& modulator, FlatSoundState& state, size_t m) {
	if (!modulator.isMap()) {
		return;
	}
	for (const auto& [key, value] : modulator.entries()) {
		if (key == "transpose") {
			state.modulator_transpose[m] = static_cast<int16_t>(state.modulator_transpose[m] + intOf(value));
		}
		else if (key == "cents") {
			state.modulator_cents[m] = cents_of(value);
		}
		else if (key == "retrigPhase") {
			state.modulator_retrigger_phase[m] = static_cast<uint32_t>(intOf(value));
		}
		else if (m == 1 && key == "toModulator1") {
			state.modulator1_to_modulator0 = intOf(value) != 0;
		}
	}
}

/// An envelope's settings read from its entry: an unknown mode is ADSR and a curve past its range is clamped, as main
/// reads them.
synth::EnvelopeState read_envelope(const Value& entry) {
	synth::EnvelopeState e;
	if (!entry.isMap()) {
		return e;
	}
	for (const auto& [key, value] : entry.entries()) {
		if (key == "mode") {
			e.mode = synth::envelope_mode_from(textOf(value));
		}
		else if (key == "curve") {
			e.curve = synth::EnvelopeState::clamped_curve(intOf(value));
		}
		else if (key == "follow") {
			e.follow = synth::envelope_follow_from(textOf(value));
		}
	}
	return e;
}

void read_lfo(const Value& lfo, FlatSoundState& state, size_t l, const plugin::CodecContext& context) {
	state.lfo_sync_type[l] = plugin::SyncType::EVEN;
	state.lfo_sync_level[l] = plugin::SyncLevel::NONE;
	if (!lfo.isMap()) {
		return;
	}
	for (const auto& [key, value] : lfo.entries()) {
		if (key == "type") {
			state.lfo_wave_type[l] = parse_lfo_type(textOf(value));
		}
		else if (key == "syncType") {
			state.lfo_sync_type[l] = static_cast<plugin::SyncType>(intOf(value));
		}
		else if (key == "syncLevel") {
			state.lfo_sync_level[l] = static_cast<plugin::SyncLevel>(
			    plugin::sync_level_from_file(intOf(value), context.input_tick_magnitude));
		}
	}
}

Value dumpRange(const RangeState& range) {
	auto map = Value::makeMap();
	map.set("topNote", number(range.top_note));
	map.set("filePath", text(range.file_path));
	map.set("startPos", Value::makeNumber(static_cast<int64_t>(range.start_pos)));
	map.set("endPos", Value::makeNumber(static_cast<int64_t>(range.end_pos)));
	map.set("loopStartPos", Value::makeNumber(range.loop_start_pos));
	map.set("loopEndPos", Value::makeNumber(range.loop_end_pos));
	map.set("transpose", number(range.transpose));
	map.set("cents", number(range.cents));
	return map;
}

} // namespace

namespace {

/// Read @p element into @p state; @p file_read says it is a file's read, whose own normalisations it applies.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<void, plugin::StateError> decode_impl(const Value& element, FlatSoundState& state,
                                                    const plugin::CodecContext& context, bool file_read) {
	if (!element.isMap()) {
		return std::unexpected(plugin::StateError::NOT_A_MAP);
	}
	// What reading a sound always starts with, whatever the file holds.
	state.modulator_transpose[1] = 0;
	state.osc_retrigger_phase = {};
	state.modulator_retrigger_phase = {};
	std::array<SourceReading, 2> reading{SourceReading{.refuseDuplicates = file_read},
	                                     SourceReading{.refuseDuplicates = file_read}};

	for (const auto& [key, value] : element.entries()) {
		if (key == "osc1" || key == "osc2") {
			if (auto read = read_source(value, state, key == "osc1" ? 0 : 1, reading[key == "osc1" ? 0 : 1]); !read) {
				return read;
			}
		}
		else if (key == "mode") {
			state.synth_mode = parse_synth_mode(textOf(value));
		}
		else if (key == "modulator1" || key == "modulator2") {
			read_modulator(value, state, key == "modulator1" ? 0 : 1);
		}
		else if (key == "transpose") {
			state.transpose = static_cast<int16_t>(intOf(value));
		}
		else if (key == "cents") {
			state.cents = cents_of(value);
		}
		else if (key == "unison") {
			if (value.isMap()) {
				for (const auto& [unison_key, unison_value] : value.entries()) {
					if (unison_key == "num") {
						state.num_unison = static_cast<uint8_t>(std::clamp(intOf(unison_value), 0, kMaxUnisonVoices));
					}
					else if (unison_key == "detune") {
						state.unison_detune = static_cast<int8_t>(std::clamp(intOf(unison_value), 0, kMaxUnisonDetune));
					}
					else if (unison_key == "spread") {
						state.unison_stereo_spread =
						    static_cast<uint8_t>(std::clamp(intOf(unison_value), 0, kMaxUnisonSpread));
					}
				}
			}
		}
		else if (key.size() == 4 && key.starts_with("lfo") && key[3] >= '1' && key[3] <= '4') {
			read_lfo(value, state, static_cast<size_t>(key[3] - '1'), context);
		}
		else if (const std::optional<size_t> e = envelope_index(key)) {
			state.envelopes[*e] = read_envelope(value);
		}
		else if (key == "polyphonic") {
			state.polyphonic = parse_polyphony_mode(textOf(value));
		}
		else if (key == "maxVoices") {
			state.max_voice_count = static_cast<uint8_t>(intOf(value));
		}
		else if (key == "voicePriority") {
			state.voice_priority = static_cast<plugin::VoicePriority>(intOf(value));
		}
	}
	// What finishing a read does to each source: FM plays sines, ring modulation takes nothing past a wavetable, and a
	// sample source's own transpose and cents move into each of its ranges.
	for (SourceState& src : state.sources) {
		if (state.synth_mode == SynthMode::FM) {
			src.osc_type = OscType::SINE;
		}
		else if (state.synth_mode == SynthMode::RINGMOD && file_read) {
			src.osc_type = std::min(src.osc_type, OscType::WAVETABLE);
		}
		if (state.synth_mode != SynthMode::FM && src.osc_type == OscType::SAMPLE) {
			for (RangeState& range : src.ranges) {
				range.transpose = static_cast<int16_t>(range.transpose + src.transpose);
				range.cents = static_cast<int8_t>(std::clamp(range.cents + src.cents, -kMaxCents, kMaxCents));
			}
			src.transpose = 0;
			src.cents = 0;
		}
	}
	// A range holds a zone and tuning only in a sample source.
	for (SourceState& src : state.sources) {
		if (src.osc_type != OscType::SAMPLE) {
			for (RangeState& range : src.ranges) {
				range = RangeState{.top_note = range.top_note, .file_path = std::move(range.file_path)};
			}
		}
	}
	deluge::effects::decode(element, state.effects, context);
	return {};
}

/// The entries of an oscillator's element that hold its ranges.
constexpr std::array<std::string_view, 4> kRangeKeys{"fileName", "zone", "sampleRanges", "wavetableRanges"};

/// Whether oscillator elements @p a and @p b hold the same ranges.
bool sameRanges(const Value* a, const Value* b) {
	for (std::string_view key : kRangeKeys) {
		const Value* inA = a != nullptr ? a->find(key) : nullptr;
		const Value* inB = b != nullptr ? b->find(key) : nullptr;
		if ((inA == nullptr) != (inB == nullptr)
		    || (inA != nullptr && !serialization::first_difference(*inA, *inB).empty())) {
			return false;
		}
	}
	return true;
}

/// The part of @p state the entry @p key of a sound element writes, copied from @p from.
void keep_entry(std::string_view key, FlatSoundState& state, const FlatSoundState& from) {
	if (key == "polyphonic") {
		state.polyphonic = from.polyphonic;
	}
	else if (key == "voicePriority") {
		state.voice_priority = from.voice_priority;
	}
	else if (key == "mode") {
		state.synth_mode = from.synth_mode;
	}
	else if (key == "transpose") {
		state.transpose = from.transpose;
	}
	else if (key == "cents") {
		state.cents = from.cents;
	}
	else if (key == "maxVoices") {
		state.max_voice_count = from.max_voice_count;
	}
	else if (key == "osc1" || key == "osc2") {
		const size_t s = key == "osc1" ? 0 : 1;
		state.sources[s] = from.sources[s];
		state.osc_retrigger_phase[s] = from.osc_retrigger_phase[s];
		if (s == 1) {
			state.oscillator_sync = from.oscillator_sync;
		}
	}
	else if (key.starts_with("lfo")) {
		const size_t l = static_cast<size_t>(key[3] - '1');
		state.lfo_wave_type[l] = from.lfo_wave_type[l];
		state.lfo_sync_type[l] = from.lfo_sync_type[l];
		state.lfo_sync_level[l] = from.lfo_sync_level[l];
	}
	else if (const std::optional<size_t> e = envelope_index(key)) {
		state.envelopes[*e] = from.envelopes[*e];
	}
	else if (key == "modulator1" || key == "modulator2") {
		const size_t m = key == "modulator1" ? 0 : 1;
		state.modulator_transpose[m] = from.modulator_transpose[m];
		state.modulator_cents[m] = from.modulator_cents[m];
		state.modulator_retrigger_phase[m] = from.modulator_retrigger_phase[m];
		if (m == 1) {
			state.modulator1_to_modulator0 = from.modulator1_to_modulator0;
		}
	}
	else if (key == "unison") {
		state.num_unison = from.num_unison;
		state.unison_detune = from.unison_detune;
		state.unison_stereo_spread = from.unison_stereo_spread;
	}
	else if (deluge::effects::is_effect_key(key)) {
		deluge::effects::keep_effect_entry(key, state.effects, from.effects);
	}
}

} // namespace

std::expected<void, plugin::StateError> decode(const Value& element, SoundState& state,
                                               const plugin::CodecContext& context) {
	FlatSoundState flat = to_flat(state);
	auto result = decode(element, flat, context);
	state = to_record(flat);
	return result;
}

std::expected<void, plugin::StateError> decode(const Value& element, FlatSoundState& state,
                                               const plugin::CodecContext& context) {
	return decode_impl(element, state, context, /*file_read=*/true);
}

void keep_unwritten(FlatSoundState& state, const FlatSoundState& from) {
	const bool fm = state.synth_mode == SynthMode::FM;
	if (!fm) {
		state.modulator_transpose = from.modulator_transpose;
		state.modulator_cents = from.modulator_cents;
		state.modulator_retrigger_phase = from.modulator_retrigger_phase;
		state.modulator1_to_modulator0 = from.modulator1_to_modulator0;
	}
	for (size_t s = 0; s < state.sources.size(); s++) {
		SourceState& src = state.sources[s];
		const SourceState& old = from.sources[s];
		const auto keepPlayback = [&] {
			src.repeat_mode = old.repeat_mode;
			src.reversed = old.reversed;
			src.pitch_and_speed_independent = old.pitch_and_speed_independent;
			src.time_stretch_amount = old.time_stretch_amount;
			src.interpolation_mode = old.interpolation_mode;
		};
		if (fm) {
			src.osc_type = old.osc_type;
			keepPlayback();
			src.ranges = old.ranges;
			src.dx_patch = old.dx_patch;
			continue;
		}
		if (src.osc_type == OscType::SAMPLE) {
			// A sample source's own tuning: the file holds its ranges', and reading folds the source's into them.
			src.transpose = old.transpose;
			src.cents = old.cents;
			state.osc_retrigger_phase[s] = from.osc_retrigger_phase[s];
			if (s == 1) {
				state.oscillator_sync = from.oscillator_sync;
			}
			src.dx_patch = old.dx_patch;
			continue;
		}
		keepPlayback();
		if (src.osc_type != OscType::WAVETABLE) {
			src.ranges = old.ranges;
		}
		if (src.osc_type != OscType::DX7) {
			src.dx_patch = old.dx_patch;
		}
	}
}

// Entry by entry: an unchanged one keeps the current state's, a changed one is read, with its legacy leaves kept:
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<FlatSlotChange, plugin::StateError> decode_change(const Value& element, const Value& previous,
                                                                const FlatSoundState& current,
                                                                const plugin::CodecContext& context) {
	FlatSlotChange change; // from a new synth's state
	FlatSoundState& decoded = change.state;
	if (auto read = decode_impl(element, decoded, context, /*file_read=*/false); !read) {
		return std::unexpected(read.error());
	}
	keep_unwritten(decoded, current);
	// An entry the element holds as the previous slot did is the current state's, whatever a file read of it would
	// make: a legacy zone, a sync level the tick magnitude cannot write. Only a changed entry is read.
	std::array<bool, 2> sourceKept{}; // an unchanged oscillator entry keeps its whole source, ranges included
	for (std::string_view key : kStateKeys) {
		if (!is_state_key(key)) {
			continue; // the host's entry: the state holds nothing of it to change
		}
		const serialization::Value* held = element.find(key);
		const serialization::Value* written = previous.find(key);
		const bool unchanged =
		    (held == nullptr && written == nullptr)
		    || (held != nullptr && written != nullptr && serialization::first_difference(*held, *written).empty());
		if (!unchanged) {
			change.changed.push_back(key);
		}
		if (unchanged) {
			keep_entry(key, decoded, current);
			if (key == "osc1" || key == "osc2") {
				sourceKept[key == "osc1" ? 0 : 1] = true;
			}
		}
		else if (key.starts_with("lfo") && held != nullptr && written != nullptr) {
			// One leaf of an LFO changing leaves its others as they are: a sync level the magnitude cannot write
			// is not re-read because the LFO's type moved.
			const size_t l = static_cast<size_t>(key[3] - '1');
			const auto same = [&](std::string_view leaf) {
				const serialization::Value* a = held->find(leaf);
				const serialization::Value* b = written->find(leaf);
				return a != nullptr && b != nullptr && serialization::first_difference(*a, *b).empty();
			};
			if (same("type")) {
				decoded.lfo_wave_type[l] = current.lfo_wave_type[l];
			}
			if (same("syncType")) {
				decoded.lfo_sync_type[l] = current.lfo_sync_type[l];
			}
			if (same("syncLevel")) {
				decoded.lfo_sync_level[l] = current.lfo_sync_level[l];
			}
		}
	}
	// A changed oscillator whose ranges are the current ones as its new type writes them carries them over whole,
	// unless they became the other kind (sample to wavetable, or back), which keeps only their top notes.
	for (size_t s = 0; s < decoded.sources.size(); s++) {
		const OscType type = decoded.sources[s].osc_type;
		const OscType was = current.sources[s].osc_type;
		const std::string_view key = s == 0 ? "osc1" : "osc2";
		const bool ranged = type == OscType::SAMPLE || type == OscType::WAVETABLE;
		const bool otherKind = (was == OscType::SAMPLE || was == OscType::WAVETABLE) && was != type;
		if (sourceKept[s] || decoded.synth_mode == SynthMode::FM || !ranged || otherKind) {
			continue;
		}
		FlatSoundState carried = current;
		carried.synth_mode = decoded.synth_mode;
		carried.sources[s].osc_type = type;
		if (sameRanges(element.find(key), encode(to_record(carried), context).find(key))) {
			decoded.sources[s].ranges = current.sources[s].ranges;
		}
	}
	return change;
}

namespace {

/// @p tree with the leaf at @p path (a/b/c) set to @p leaf, in place or appended where it was absent.
void set_leaf(Value& tree, std::string_view path, Value leaf) {
	const size_t slash = path.find('/');
	const std::string_view key = path.substr(0, slash);
	if (slash == std::string_view::npos) {
		tree.replace_or_append(key, std::move(leaf));
		return;
	}
	if (tree.find_mut(key) == nullptr) {
		tree.replace_or_append(key, Value::makeMap());
	}
	set_leaf(*tree.find_mut(key), path.substr(slash + 1), std::move(leaf));
}

} // namespace

namespace {

/// dump_state() of @p record, whose flat layout is @p state: its encoding's entries, then every field by name.
Value dump_flat(const SoundState& record, const FlatSoundState& state) {
	// The effects are compared on their own (their defaults are the song's, not the instrument's).
	Value map = Value::makeMap();
	for (const auto& [key, value] : encode(record, {.input_tick_magnitude = 1}).entries()) {
		if (!deluge::effects::is_effect_key(key)) {
			map.set(key, value);
		}
	}
	map.set("voicePriorityIndex", number(std::to_underlying(state.voice_priority)));
	map.set("oscillatorSync", number(state.oscillator_sync ? 1 : 0));
	map.set("syncTypes", text(std::to_string(std::to_underlying(state.lfo_sync_type[0])) + ","
	                          + std::to_string(std::to_underlying(state.lfo_sync_type[1])) + ","
	                          + std::to_string(std::to_underlying(state.lfo_sync_type[2])) + ","
	                          + std::to_string(std::to_underlying(state.lfo_sync_type[3]))));
	map.set("modulatorTranspose",
	        text(std::to_string(state.modulator_transpose[0]) + "," + std::to_string(state.modulator_transpose[1])));
	map.set("modulatorCents",
	        text(std::to_string(state.modulator_cents[0]) + "," + std::to_string(state.modulator_cents[1])));
	map.set("modulatorPhases", text(std::to_string(state.modulator_retrigger_phase[0]) + ","
	                                + std::to_string(state.modulator_retrigger_phase[1])));
	map.set("toModulator1", number(state.modulator1_to_modulator0 ? 1 : 0));
	for (size_t s = 0; s < state.sources.size(); s++) {
		const SourceState& src = state.sources[s];
		auto source = Value::makeMap();
		source.set("oscType", text(to_string(src.osc_type)));
		source.set("transpose", number(src.transpose));
		source.set("cents", number(src.cents));
		source.set("repeatMode", number(std::to_underlying(src.repeat_mode)));
		source.set("timeStretchAmount", number(src.time_stretch_amount));
		source.set("interpolation", number(std::to_underlying(src.interpolation_mode)));
		source.set("pitchAndSpeedIndependent", number(src.pitch_and_speed_independent ? 1 : 0));
		source.set("reversed", number(src.reversed ? 1 : 0));
		if (src.dx_patch) {
			source.set("dx7patch", text(hex(src.dx_patch->params)));
			source.set("dx7engineMode", number(src.dx_patch->engine_mode));
			source.set("dx7randomDetune", number(src.dx_patch->random_detune));
		}
		auto ranges = Value::makeArray();
		for (const RangeState& range : src.ranges) {
			ranges.append(dumpRange(range));
		}
		source.set("ranges", std::move(ranges));
		map.set("source" + std::to_string(s + 1), std::move(source));
	}
	return map;
}

/// @p change composed into a record's.
std::expected<SlotChange, plugin::StateError> composed(std::expected<FlatSlotChange, plugin::StateError> change) {
	return std::move(change).transform([](FlatSlotChange flat) {
		return SlotChange{.state = to_record(flat.state), .changed = std::move(flat.changed)};
	});
}

} // namespace

std::expected<SlotChange, plugin::StateError> decode_change(const Value& element, const Value& previous,
                                                            const SoundState& current,
                                                            const plugin::CodecContext& context) {
	return composed(decode_change(element, previous, to_flat(current), context));
}

std::expected<SlotChange, plugin::StateError> with_setting(const SoundState& state, std::string_view path,
                                                           const serialization::Value& value,
                                                           const plugin::CodecContext& context) {
	const std::string_view key = path.substr(0, path.find('/'));
	// The slot as far as this write reads it: the entry it changes, and the mode its reading may depend on. Every other
	// entry is absent from both, so it compares equal and keeps the state's value.
	Value previous = Value::makeMap();
	for (const std::string_view k : {key, std::string_view{"mode"}}) {
		if (previous.find(k) == nullptr) {
			if (std::optional<Value> entry = encode_entry(state, k, context)) {
				previous.set(std::string(k), std::move(*entry));
			}
		}
	}
	Value element = previous;
	Value leaf = value;
	if (path.ends_with("/syncLevel")) { // a sync level is written relative to the slot's magnitude
		leaf = number(plugin::sync_level_to_file(intOf(value), context.input_tick_magnitude));
	}
	set_leaf(element, path, std::move(leaf));
	return decode_change(element, previous, state, context);
}

std::vector<std::string_view> differing_entries(const SoundState& a, const SoundState& b,
                                                const plugin::CodecContext& context) {
	std::vector<std::string_view> keys;
	for (const std::string_view key : kStateKeys) {
		const std::optional<Value> ea = encode_entry(a, key, context);
		const std::optional<Value> eb = encode_entry(b, key, context);
		if (ea.has_value() != eb.has_value() || (ea && !(*ea == *eb))) {
			keys.push_back(key);
		}
	}
	return keys;
}

std::expected<SlotChange, plugin::StateError> change_to(const SoundState& state, const SoundState& next,
                                                        std::span<const std::string_view> keys,
                                                        const plugin::CodecContext& context) {
	Value previous = Value::makeMap();
	Value element = Value::makeMap();
	const auto add = [&](std::string_view key) {
		if (previous.find(key) != nullptr || element.find(key) != nullptr) {
			return;
		}
		if (std::optional<Value> was = encode_entry(state, key, context)) {
			previous.set(std::string(key), std::move(*was));
		}
		if (std::optional<Value> now = encode_entry(next, key, context)) {
			element.set(std::string(key), std::move(*now));
		}
	};
	for (const std::string_view key : keys) {
		add(key);
	}
	add("mode"); // a source's and a modulator's reading depends on it
	return decode_change(element, previous, state, context);
}

std::expected<SoundState, plugin::StateError> decode_onto(const Value& element, const SoundState& current,
                                                          const plugin::CodecContext& context) {
	return decode_change(element, encode(current, context), current, context).transform([](SlotChange change) {
		return std::move(change.state);
	});
}

serialization::Value dump_state(const SoundState& state) {
	return dump_flat(state, to_flat(state));
}

} // namespace deluge::instruments::classic
