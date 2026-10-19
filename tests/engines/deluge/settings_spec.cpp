#include "engine-api/types/arp.h"
#include "engine-api/types/setting.h"
#include "engine-api/types/voice_priority.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "engines/effects/settings.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace deluge::instrument;
using deluge::engine::DisplayMappingKind;
using deluge::engine::SettingContext;
using deluge::engine::SettingKind;
using deluge::engine::SettingUnits;
using deluge::serialization::Value;

namespace {
/// What a test reads where from_menu refuses a menu value.
constexpr std::pair<int32_t, int32_t> kNone{-1, -1};

// A new song's input tick magnitude, at which a file's sync levels 0–9 are the internal ones.
constexpr deluge::engine::CodecContext kContext{.input_tick_magnitude = 1};

/// A reader that holds a sound's synth mode alone, and records any other path a condition asks for.
struct ModeReader {
	int32_t mode{0};
	mutable std::vector<std::string> unknown{};

	[[nodiscard]] deluge::engine::SettingReader reader() const {
		return {.read = [](const void* self, std::string_view path) -> std::optional<int32_t> {
			        const auto& held = *static_cast<const ModeReader*>(self);
			        if (path != "mode") {
				        held.unknown.emplace_back(path);
				        return std::nullopt;
			        }
			        return held.mode;
		        },
		        .self = this};
	}
};

SoundState fmState() {
	SoundState state = new_synth_state();
	state.synth_mode = SynthMode::FM;
	return state;
}

SoundState sampleState() {
	SoundState state = new_synth_state();
	for (SourceState& source : state.sources) {
		source.osc_type = OscType::SAMPLE;
		source.ranges.push_back(RangeState{.file_path = "SAMPLES/x.wav"});
	}
	return state;
}

/// Every scalar leaf path of @p node ("osc1/type"…), stopping at arrays.
void leafPaths(const Value& node, const std::string& prefix, std::vector<std::string>& into) {
	if (node.isMap()) {
		for (const auto& [key, value] : node.entries()) {
			leafPaths(value, prefix.empty() ? key : prefix + "/" + key, into);
		}
	}
	else {
		into.push_back(prefix);
	}
}

/// Whether @p path is a row of the schema or lies under a STRUCTURED row.
bool covered(const std::string& path) {
	for (const deluge::engine::SettingInfo& row : kSettings) {
		if (row.path == path) {
			return true;
		}
		if (row.kind == SettingKind::STRUCTURED && path.starts_with(std::string(row.path) + "/")) {
			return true;
		}
	}
	return false;
}

/// @p tree with the leaf at @p path set to @p text.
Value withLeaf(const Value& tree, std::string_view path, const std::string& text) {
	const size_t slash = path.find('/');
	const std::string_view head = path.substr(0, slash);
	Value out = Value::makeMap();
	bool done = false;
	for (const auto& [key, value] : tree.entries()) {
		if (key == head) {
			out.set(key, slash == std::string_view::npos ? Value::makeScalar(text)
			                                             : withLeaf(value, path.substr(slash + 1), text));
			done = true;
		}
		else {
			out.set(key, value);
		}
	}
	if (!done) {
		out.set(std::string(head), slash == std::string_view::npos
		                               ? Value::makeScalar(text)
		                               : withLeaf(Value::makeMap(), path.substr(slash + 1), text));
	}
	return out;
}

const Value* leafAt(const Value& tree, std::string_view path) {
	const Value* at = &tree;
	while (at != nullptr && !path.empty()) {
		const size_t slash = path.find('/');
		at = at->isMap() ? at->find(path.substr(0, slash)) : nullptr;
		path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
	}
	return at;
}

/// The state a row's path is written in: modulators in FM, sample playback on sample sources.
SoundState baseFor(std::string_view path) {
	if (path.starts_with("modulator")) {
		return fmState();
	}
	for (std::string_view sampleOnly : {"loopMode", "reversed", "timeStretch", "linearInterpolation"}) {
		if (path.find(sampleOnly) != std::string_view::npos) {
			return sampleState();
		}
	}
	return new_synth_state();
}

/// The values a row allows at its edges: every choice, or the range's ends (and, for a row with a user scale, the
/// scale's stored ends, the value past them and each sentinel's stored value), or both bools.
std::vector<std::string> edges(const deluge::engine::SettingInfo& row) {
	switch (row.kind) {
	case SettingKind::CHOICE:
		return {row.choices.begin(), row.choices.end()};
	case SettingKind::INTEGER: {
		std::vector<std::string> values{std::to_string(row.min), std::to_string(row.max)};
		if (!row.user.identity()) {
			for (const int64_t v :
			     {int64_t{row.user.stored_min}, int64_t{row.user.stored_max}, int64_t{row.user.stored_max} + 1}) {
				if (v >= row.min && v <= row.max) {
					values.push_back(std::to_string(v));
				}
			}
			for (const deluge::engine::SettingSentinel& sentinel : row.user.sentinels) {
				values.push_back(std::to_string(sentinel.stored));
			}
		}
		return values;
	}
	case SettingKind::BOOL:
		return {"0", "1"};
	case SettingKind::STRUCTURED:
	case SettingKind::TEXT:
		break;
	}
	return {};
}
} // namespace

// clang-format off
describe settings("the Deluge instrument's settings schema", ${
	it("covers every setting the instrument writes, in each mode", _ {
		for (const SoundState& state : {new_synth_state(), fmState(), sampleState()}) {
			std::vector<std::string> paths;
			leafPaths(encode(state, kContext), "", paths);
			for (const std::string& path : paths) {
				expect(path).to_satisfy([](const std::string& p) { return covered(p); });
			}
		}
	});

	it("keeps every value a row allows through decode and encode", _ {
		for (const deluge::engine::SettingInfo& row : kSettings) {
			for (const std::string& value : edges(row)) {
				const SoundState base = baseFor(row.path);
				SoundState read = new_synth_state();
				expect(decode(withLeaf(encode(base, kContext), row.path, value), read, kContext)).to_have_value();
				const Value reencoded = encode(read, kContext);
				const Value* again = leafAt(reencoded, row.path);
				// a value the encoding leaves out when it is the default is read from the state it decoded to
				const auto held = setting_value(read, row.path);
				const std::string got = again != nullptr ? *deluge::serialization::text_of(again)
				                        : held           ? *deluge::serialization::text_of(&*held)
				                                         : std::string("0");
				expect(std::string(row.path) + "=" + got).to_equal(std::string(row.path) + "=" + value);
			}
		}
	});

	it("refuses a value a row does not allow, and every structured setting", _ {
		expect(check_setting("unison/num", Value::makeNumber(9))).to_be_false();
		expect(check_setting("unison/num", Value::makeNumber(3))).to_be_true();
		expect(check_setting("polyphonic", Value::makeScalar("sideways"))).to_be_false();
		expect(check_setting("polyphonic", Value::makeScalar("poly"))).to_be_true();
		expect(check_setting("osc2/oscillatorSync", Value::makeNumber(2))).to_be_false();
		expect(check_setting("osc1/dx7patch", Value::makeScalar("00"))).to_be_false();
		expect(check_setting("no/such/path", Value::makeNumber(0))).to_be_false();
		expect(check_setting("unison/num", Value::makeScalar("three"))).to_be_false();
	});

	it("writes an integer the way a file does, however it was given", _ {
		expect(canonical_setting("unison/num", Value::makeScalar("03"))->str()).to_equal(std::string("3"));
		expect(canonical_setting("polyphonic", Value::makeScalar("mono"))->str()).to_equal(std::string("mono"));
		expect(canonical_setting("unison/num", Value::makeScalar("9"))).not_().to_have_value();
	});

	it("offers voice priority as a choice whose words are the numbers a file writes, in the engine's order", _ {
		const deluge::engine::SettingInfo* row = deluge::engine::find_setting(kSettings, "voicePriority");
		expect(row->kind == SettingKind::CHOICE).to_be_true();
		for (const auto priority : {deluge::engine::VoicePriority::LOW, deluge::engine::VoicePriority::MEDIUM,
		                            deluge::engine::VoicePriority::HIGH}) {
			const std::string word = std::to_string(std::to_underlying(priority));
			const size_t at = std::to_underlying(priority);
			expect(at < row->choices.size() ? std::string(row->choices[at]) : std::string()).to_equal(word);
			expect(check_setting("voicePriority", Value::makeNumber(std::to_underlying(priority)))).to_be_true();
		}
		expect(row->choices.size()).to_equal(size_t{3});
		expect(check_setting("voicePriority", Value::makeNumber(3))).to_be_false();
		expect(check_setting("voicePriority", Value::makeScalar("02"))).to_be_false();
		expect(canonical_setting("voicePriority", Value::makeNumber(2))->str()).to_equal(std::string("2"));
	});

	it("shows max voices as legacy's menu does: 1–16 as themselves, the limit past 16 as off, and off as 127", _ {
		const deluge::engine::SettingInfo& row = *deluge::engine::find_setting(kSettings, "maxVoices");
		expect(check_setting("maxVoices", Value::makeNumber(1))).to_be_true();
		expect(check_setting("maxVoices", Value::makeNumber(127))).to_be_true();
		expect(check_setting("maxVoices", Value::makeNumber(0))).to_be_false();
		expect(check_setting("maxVoices", Value::makeNumber(128))).to_be_false();
		// `VoiceCount::readCurrentValue`: a count above 16 reads as 0 (off)
		expect(deluge::engine::to_menu(row, 1)).to_equal(1);
		expect(deluge::engine::to_menu(row, 16)).to_equal(16);
		expect(deluge::engine::to_menu(row, 17)).to_equal(0);
		expect(deluge::engine::to_menu(row, 126)).to_equal(0);
		expect(deluge::engine::to_menu(row, 127)).to_equal(0);
		// `VoiceCount::writeCurrentValue`: 0 writes 127
		expect(deluge::engine::from_menu(row, 0).value_or(kNone).first).to_equal(127);
		expect(deluge::engine::from_menu(row, 1).value_or(kNone).first).to_equal(1);
		expect(deluge::engine::from_menu(row, 16).value_or(kNone).first).to_equal(16);
		expect(deluge::engine::from_menu(row, 17)).not_().to_have_value();
	});

	it("offers choke on a kit's lane, and not on a track", _ {
		const deluge::engine::SettingInfo& row = *deluge::engine::find_setting(kSettings, "polyphonic");
		const auto choke = static_cast<size_t>(PolyphonyMode::CHOKE);
		const auto mono = static_cast<size_t>(PolyphonyMode::MONO);
		expect(std::string(row.choices[choke])).to_equal(std::string("choke"));
		expect(deluge::engine::setting_choice_applies(row, {.owner = SettingContext::Owner::KIT_LANE}, {}, choke))
		    .to_be_true();
		expect(deluge::engine::setting_choice_applies(row, {.owner = SettingContext::Owner::TRACK}, {}, choke))
		    .to_be_false();
		expect(deluge::engine::setting_choice_applies(row, {.owner = SettingContext::Owner::TRACK}, {}, mono)).to_be_true();
	});

	it("lists the oscillator types by the owner's synth mode, read by path, its owner and its inputs", _ {
		const deluge::engine::SettingInfo& row = *deluge::engine::find_setting(kSettings, "osc1/type");
		const auto sample = static_cast<size_t>(OscType::SAMPLE);
		const auto dx7 = static_cast<size_t>(OscType::DX7);
		const auto input_r = static_cast<size_t>(OscType::INPUT_R);
		using Owner = SettingContext::Owner;
		using Instrument = SettingContext::Instrument;
		const SettingContext track{.owner = Owner::TRACK, .instrument = Instrument::SYNTH};
		const SettingContext lane{.owner = Owner::KIT_LANE, .instrument = Instrument::SOUND_DRUM};
		const SettingContext whole_kit{.owner = Owner::KIT, .instrument = Instrument::KIT};
		const SettingContext plugged{.owner = Owner::TRACK, .instrument = Instrument::SYNTH, .audio_inputs_present = true};
		for (const SettingContext& shown : {track, lane, whole_kit, plugged}) {
			expect(deluge::engine::context_is_coherent(shown)).to_be_true();
		}
		const ModeReader subtractive{static_cast<int32_t>(SynthMode::SUBTRACTIVE)};
		const ModeReader ring{static_cast<int32_t>(SynthMode::RINGMOD)};
		expect(deluge::engine::setting_choice_applies(row, track, subtractive.reader(), sample)).to_be_true();
		expect(deluge::engine::setting_choice_applies(row, track, ring.reader(), sample)).to_be_false();
		expect(deluge::engine::applicable_choice_count(row, track, ring.reader())).to_equal(7);
		expect(deluge::engine::applicable_choice_count(row, track, subtractive.reader())).to_equal(10);
		expect(deluge::engine::applicable_choice_count(row, plugged, subtractive.reader())).to_equal(12);
		expect(deluge::engine::setting_choice_applies(row, track, subtractive.reader(), dx7)).to_be_true();
		expect(deluge::engine::setting_choice_applies(row, lane, subtractive.reader(), dx7)).to_be_false();
		expect(deluge::engine::setting_choice_applies(row, whole_kit, subtractive.reader(), dx7)).to_be_false();
		expect(deluge::engine::setting_choice_applies(row, track, subtractive.reader(), input_r)).to_be_false();
		expect(deluge::engine::setting_choice_applies(row, plugged, subtractive.reader(), input_r)).to_be_true();
		expect(deluge::engine::setting_choice_applies(row, plugged, ring.reader(), input_r)).to_be_false();
		expect(ring.unknown.empty()).to_be_true();
		expect(subtractive.unknown.empty()).to_be_true();
	});

	it("maps a filtered choice row between its stored index and its menu position", _ {
		const deluge::engine::SettingInfo& row = *deluge::engine::find_setting(kSettings, "osc2/type");
		const SettingContext track{.owner = SettingContext::Owner::TRACK, .instrument = SettingContext::Instrument::SYNTH};
		expect(deluge::engine::context_is_coherent(track)).to_be_true();
		const ModeReader subtractive{static_cast<int32_t>(SynthMode::SUBTRACTIVE)};
		const auto reader = subtractive.reader();
		const auto input_l = static_cast<int32_t>(OscType::INPUT_L);
		expect(input_l).to_equal(9);
		expect(deluge::engine::choice_position(row, track, reader, input_l).value_or(-1)).to_equal(8);
		expect(deluge::engine::choice_at_position(row, track, reader, 8).value_or(-1)).to_equal(input_l);
		for (int32_t position = 0; position < deluge::engine::applicable_choice_count(row, track, reader); ++position) {
			const std::optional<int32_t> stored = deluge::engine::choice_at_position(row, track, reader, position);
			expect(stored).to_have_value();
			expect(deluge::engine::choice_position(row, track, reader, *stored).value_or(-1)).to_equal(position);
		}
		// the second oscillator never offers DX7 (index 8)
		expect(deluge::engine::choice_position(row, track, reader, static_cast<int32_t>(OscType::DX7)))
		    .not_()
		    .to_have_value();
		expect(deluge::engine::choice_at_position(row, track, reader, 9)).not_().to_have_value();
		expect(deluge::engine::choice_at_position(row, track, reader, -1)).not_().to_have_value();
	});

	it("reads, for a condition, the value a sound holds where its menu does not show it", _ {
		SoundState fm = new_synth_state();
		fm.synth_mode = SynthMode::FM;
		fm.sources[0].osc_type = OscType::SAMPLE;
		expect(setting_value(fm, "osc1/type")).not_().to_have_value();
		expect(held_setting(fm, "osc1/type").value_or(-1)).to_equal(static_cast<int32_t>(OscType::SAMPLE));
		expect(held_setting(fm, "mode").value_or(-1)).to_equal(static_cast<int32_t>(SynthMode::FM));
		const SoundState subtractive = new_synth_state();
		expect(setting_value(subtractive, "modulator1/transpose")).not_().to_have_value();
		expect(held_setting(subtractive, "modulator1/transpose")).to_have_value();
		expect(held_setting(subtractive, "osc1/sampleRanges")).not_().to_have_value();
		expect(held_setting(subtractive, "no/such/path")).not_().to_have_value();
	});

	it("shows each LFO's sync as one menu value over its level and its type", _ {
		for (const std::string lfo : {"lfo1", "lfo2", "lfo3", "lfo4"}) {
			const deluge::engine::SettingInfo& row = *deluge::engine::find_setting(kSettings, lfo + "/syncLevel");
			const deluge::engine::DisplayMapping& mapping = deluge::engine::mapping_of(row);
			expect(mapping.kind == DisplayMappingKind::COMBINED_SYNC).to_be_true();
			expect(std::string(mapping.partner)).to_equal(lfo + "/syncType");
			const deluge::engine::SettingInfo* partner = deluge::engine::find_setting(kSettings, mapping.partner);
			expect(partner != nullptr && partner->kind == SettingKind::CHOICE).to_be_true();
			expect(row.units == SettingUnits::SYNC_LEVEL).to_be_true();
			expect(deluge::engine::to_menu(row, 3, 1)).to_equal(12); // the third triplet level
			expect(deluge::engine::from_menu(row, deluge::engine::kMaxCombinedSync).value_or(kNone))
			    .to_equal(std::pair<int32_t, int32_t>{9, 2});
		}
	});

	it("keeps ring mod to the oscillators it can ring", _ {
		expect(allows_setting(sampleState(), "mode", Value::makeScalar("ringmod"))).to_be_false();
		expect(allows_setting(new_synth_state(), "mode", Value::makeScalar("ringmod"))).to_be_true();
		SoundState ringing = new_synth_state();
		ringing.synth_mode = SynthMode::RINGMOD;
		expect(allows_setting(ringing, "osc1/type", Value::makeScalar("sample"))).to_be_false();
		expect(allows_setting(ringing, "osc1/type", Value::makeScalar("wavetable"))).to_be_true();
		expect(allows_setting(sampleState(), "mode", Value::makeScalar("fm"))).to_be_true();
	});

	it("starts each instrument row at a new sound's value, in each mode", _ {
		SoundState sample = new_synth_state();
		for (SourceState& source : sample.sources) {
			source.osc_type = OscType::SAMPLE;
		}
		int checked = 0;
		for (const SoundState& state : {new_synth_state(), fmState(), sample}) {
			for (const deluge::engine::SettingInfo& row : kSettings) {
				if (row.kind == SettingKind::STRUCTURED
				    || deluge::engine::find_setting(deluge::effects::kEffectSettings, row.path) != nullptr) {
					continue;
				}
				// The FM and sample states change the mode and the oscillator types themselves.
				const bool changed = row.path == "mode" || row.path.ends_with("/type");
				if (changed && !(state == new_synth_state())) {
					continue;
				}
				const std::optional<Value> value = setting_value(state, row.path);
				if (!value) {
					continue;
				}
				int32_t stored = 0;
				if (row.kind == SettingKind::CHOICE) {
					for (size_t i = 0; i < row.choices.size(); ++i) {
						if (row.choices[i] == value->str()) {
							stored = static_cast<int32_t>(i);
						}
					}
				}
				else {
					stored = static_cast<int32_t>(*deluge::engine::whole_number(value->str()));
				}
				expect(std::string{row.path} + "=" + std::to_string(row.default_value))
				    .to_equal(std::string{row.path} + "=" + std::to_string(stored));
				++checked;
			}
		}
		expect(checked > 60).to_be_true();
	});

	it("gives a clip its arp's settings as the instrument's clip-scoped rows, apart from its sound's", _ {
		expect(kClipScopedSettings.size()).to_equal(deluge::engine::kArpSettingRows.size());
		for (const deluge::engine::SettingInfo& row : kClipScopedSettings) {
			expect(deluge::engine::find_setting(deluge::engine::kArpSettingRows, row.path) != nullptr).to_be_true();
			expect(deluge::engine::find_setting(kSettings, row.path) == nullptr).to_be_true();
		}
	});

	it("turns every stored value of every row into its file text and back", _ {
		std::array<char, 16> scratch{};
		for (const deluge::engine::SettingInfo& row : kSettings) {
			if (row.kind == SettingKind::STRUCTURED || row.kind == SettingKind::TEXT) {
				continue;
			}
			const auto [least, greatest] = deluge::engine::stored_bounds(row);
			for (const int32_t stored : {least, greatest, std::clamp(0, least, greatest)}) {
				const std::optional<std::string_view> text = deluge::engine::stored_text(row, stored, scratch);
				expect(std::string(row.path) + (text ? " has text" : " has none"))
				    .to_equal(std::string(row.path) + " has text");
				if (text) {
					expect(deluge::engine::stored_value(row, *text).value_or(-12345)).to_equal(stored);
				}
			}
		}
	});

	it("reads a choice's word as its index and refuses text no row holds", _ {
		const deluge::engine::SettingInfo& polyphonic = *deluge::engine::find_setting(kSettings, "polyphonic");
		const auto poly = std::ranges::find(polyphonic.choices, std::string_view{"poly"}) - polyphonic.choices.begin();
		expect(deluge::engine::stored_value(polyphonic, "poly").value_or(-1)).to_equal(static_cast<int32_t>(poly));
		expect(deluge::engine::stored_value(polyphonic, "junk").has_value()).to_be_false();
		const deluge::engine::SettingInfo& unison = *deluge::engine::find_setting(kSettings, "unison/num");
		expect(deluge::engine::stored_value(unison, "junk").has_value()).to_be_false();
		std::array<char, 16> scratch{};
		expect(deluge::engine::stored_text(polyphonic, 99, scratch).has_value()).to_be_false();
	});
});

CPPSPEC_SPEC(settings)
