#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <string>
#include <vector>

using namespace deluge::instrument;
using deluge::engine::SettingKind;
using deluge::serialization::Value;

namespace {
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

/// The values a row allows at its edges: every choice, or the range's ends, or both bools.
std::vector<std::string> edges(const deluge::engine::SettingInfo& row) {
	switch (row.kind) {
	case SettingKind::CHOICE:
		return {row.choices.begin(), row.choices.end()};
	case SettingKind::INTEGER:
		return {std::to_string(row.min), std::to_string(row.max)};
	case SettingKind::BOOL:
		return {"0", "1"};
	case SettingKind::STRUCTURED:
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
				// a value the encoding leaves out when it is the default reads back absent
				const std::string got = again != nullptr ? *deluge::serialization::text_of(again) : std::string("0");
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
});

CPPSPEC_SPEC(settings)
