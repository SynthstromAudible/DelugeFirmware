#include "engine-api/types/arp.h"
#include "engine-api/types/setting.h"
#include "engine-api/types/voice_priority.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <optional>
#include <string>
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
		expect(deluge::engine::setting_choice_applies(row, {.owner = SettingContext::Owner::KIT_LANE}, choke))
		    .to_be_true();
		expect(deluge::engine::setting_choice_applies(row, {.owner = SettingContext::Owner::TRACK}, choke))
		    .to_be_false();
		expect(deluge::engine::setting_choice_applies(row, {.owner = SettingContext::Owner::TRACK}, mono)).to_be_true();
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

	it("gives a clip its arp's settings as the instrument's clip-scoped rows, apart from its sound's", _ {
		expect(kClipScopedSettings.size()).to_equal(deluge::engine::kArpSettingRows.size());
		for (const deluge::engine::SettingInfo& row : kClipScopedSettings) {
			expect(deluge::engine::find_setting(deluge::engine::kArpSettingRows, row.path) != nullptr).to_be_true();
			expect(deluge::engine::find_setting(kSettings, row.path) == nullptr).to_be_true();
		}
	});
});

CPPSPEC_SPEC(settings)
