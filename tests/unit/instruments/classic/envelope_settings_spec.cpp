#include "instruments/classic/codec.h"
#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/settings.h"
#include "plugin/types/setting.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"

#include "instruments/classic/state_access.h"
#include "support/record_state.h"
#include <cppspec.hpp>
#include <string>
#include <vector>

using namespace deluge::instruments::classic;
using deluge::serialization::Value;
using deluge::synth::EnvelopeFollow;
using deluge::synth::EnvelopeMode;
using deluge::synth::EnvelopeState;

// Each envelope's mode, curve and follow input: settings of the sound, written as main writes them
// (`<envelopeNSettings mode curve follow/>` after the LFOs, only when not classic ADSR, `follow` only when not Z).

namespace {
constexpr deluge::plugin::CodecContext kContext{.input_tick_magnitude = 1};

std::vector<std::string> keys(const Value& map) {
	std::vector<std::string> out;
	for (const auto& [key, value] : map.entries()) {
		out.push_back(key);
	}
	return out;
}
std::string text(const Value& map, const char* key) {
	const Value* v = map.find(key);
	return v != nullptr && v->isScalar() ? v->str() : std::string("(none)");
}
/// @p xml's sound read into a new synth's state.
SoundState read(const std::string& xml) {
	auto doc = deluge::serialization::parseXml(xml);
	SoundState state;
	(void)decode(*doc->find("sound"), state, kContext);
	return state;
}
} // namespace

// clang-format off
describe envelope_settings("an envelope's mode, curve and follow input", ${
	it("writes nothing for a classic ADSR envelope", _ {
		const SoundPieces p = encode_pieces(SoundState{}, kContext);
		expect(keys(p.body)).to_equal(std::vector<std::string>{"osc1", "osc2", "lfo1", "lfo2", "lfo3", "lfo4", "unison"});
	});

	it("writes a changed envelope after the LFOs, its follow input only when not Z", _ {
		SoundState s;
		deluge::test::set(s, "envelope2Settings/mode", "ahd");
		deluge::test::set(s, "envelope4Settings/curve", "-7");
		deluge::test::set(s, "envelope4Settings/follow", "y");
		const SoundPieces p = encode_pieces(s, kContext);
		expect(keys(p.body)).to_equal(std::vector<std::string>{"osc1", "osc2", "lfo1", "lfo2", "lfo3", "lfo4",
		                                                       "envelope2Settings", "envelope4Settings", "unison"});
		const Value& two = *p.body.find("envelope2Settings");
		expect(keys(two)).to_equal(std::vector<std::string>{"mode", "curve"});
		expect(text(two, "mode")).to_equal(std::string("ahd"));
		expect(text(two, "curve")).to_equal(std::string("32"));
		const Value& four = *p.body.find("envelope4Settings");
		expect(keys(four)).to_equal(std::vector<std::string>{"mode", "curve", "follow"});
		expect(text(four, "mode")).to_equal(std::string("adsr"));
		expect(text(four, "curve")).to_equal(std::string("-7"));
		expect(text(four, "follow")).to_equal(std::string("y"));
	});

	it("reads back every mode and curve it writes", _ {
		for (const char* mode : {"adsr", "ahd", "ar", "mpe"}) {
			for (const char* curve : {"-50", "0", "7", "50"}) {
				SoundState s;
				deluge::test::set(s, "envelope3Settings/mode", mode);
				deluge::test::set(s, "envelope3Settings/curve", curve);
				deluge::test::set(s, "envelope3Settings/follow", "y");
				SoundState back;
				expect(decode(encode(s, kContext), back, kContext).has_value()).to_be_true();
				expect(envelope(back, 2)).to_equal(envelope(s, 2));
				expect(envelope(back, 2).curve).to_equal(static_cast<int8_t>(std::stoi(curve)));
			}
		}
	});

	it("reads a sound main wrote", _ {
		const SoundState s =
		    read(R"(<sound mode="subtractive"><envelope2Settings mode="ahd" curve="-12" follow="y"/></sound>)");
		expect(envelope(s, 1)).to_equal(EnvelopeState{.mode = EnvelopeMode::AHD, .follow = EnvelopeFollow::Y, .curve = -12});
		expect(envelope(s, 0)).to_equal(EnvelopeState{});
	});

	it("reads an unknown mode as ADSR and clamps a curve past its range", _ {
		const SoundState s =
		    read(R"(<sound mode="subtractive"><envelope1Settings mode="banana" curve="99"/></sound>)");
		expect(envelope(s, 0).mode).to_equal(EnvelopeMode::ADSR);
		expect(envelope(s, 0).curve).to_equal(int8_t{50});
		const SoundPieces p = encode_pieces(s, kContext);
		expect(text(*p.body.find("envelope1Settings"), "curve")).to_equal(std::string("50"));
	});

	it("holds rows for each envelope's mode, curve and follow", _ {
		for (const std::string e : {"envelope1Settings", "envelope2Settings", "envelope3Settings", "envelope4Settings"}) {
			const deluge::plugin::SettingInfo* mode = deluge::plugin::find_setting(kSettings, e + "/mode");
			const deluge::plugin::SettingInfo* curve = deluge::plugin::find_setting(kSettings, e + "/curve");
			const deluge::plugin::SettingInfo* follow = deluge::plugin::find_setting(kSettings, e + "/follow");
			expect(mode != nullptr).to_equal(true);
			expect(curve != nullptr).to_equal(true);
			expect(follow != nullptr).to_equal(true);
			if (mode == nullptr || curve == nullptr || follow == nullptr) {
				continue;
			}
			expect(std::vector<std::string_view>(mode->choices.begin(), mode->choices.end()))
			    .to_equal(std::vector<std::string_view>{"adsr", "ahd", "ar", "mpe"});
			expect(curve->min).to_equal(-50);
			expect(curve->max).to_equal(50);
			expect(curve->default_value).to_equal(32);
			expect(std::vector<std::string_view>(follow->choices.begin(), follow->choices.end()))
			    .to_equal(std::vector<std::string_view>{"z", "y"});
		}
	});

	it("gives each envelope's settings to the voices' config", _ {
		SoundState s;
		deluge::test::set(s, "envelope3Settings/mode", "mpe");
		deluge::test::set(s, "envelope3Settings/curve", "-20");
		const deluge::synth::SoundConfig config = DelugeSynth::derive(s);
		expect(config.envelopes[2]).to_equal(EnvelopeState{.mode = EnvelopeMode::MPE, .curve = -20});
		expect(config.envelopes[0]).to_equal(EnvelopeState{});
	});
});
// clang-format on

CPPSPEC_SPEC(envelope_settings)
