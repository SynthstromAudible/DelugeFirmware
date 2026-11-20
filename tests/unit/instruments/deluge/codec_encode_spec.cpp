#include "instruments/deluge/codec.h"

#include "instruments/deluge/state_access.h"
#include "support/record_state.h"
#include <cppspec.hpp>
#include <string>
#include <utility>
#include <vector>

using namespace deluge::instrument;
using deluge::serialization::Value;

namespace {
std::vector<std::string> keys(const Value& map) {
	std::vector<std::string> out;
	for (const auto& [key, value] : map.entries()) {
		out.push_back(key);
	}
	return out;
}
/// Adds @p range to slot @p id's ranges in @p s.
void add_range(SoundState& s, SourceId id, RangeState range) {
	std::vector<RangeState> ranges = source_ranges(s, id);
	ranges.push_back(std::move(range));
	s = with_ranges(std::move(s), id, std::move(ranges));
}
std::string text(const Value& map, const char* key) {
	const Value* v = map.find(key);
	return v != nullptr && v->isScalar() ? v->str() : std::string("(none)");
}
} // namespace

// clang-format off
describe codec_encode("the Deluge instrument writes its state", ${
	it("writes a sound's head without the values that are zero", _ {
		SoundState s;
		deluge::test::set(s, "polyphonic", to_string(PolyphonyMode::POLY));
		SoundPieces p = encode_pieces(s, {.input_tick_magnitude = 1});
		expect(keys(p.head)).to_equal(std::vector<std::string>{"polyphonic", "voicePriority", "mode"});
		expect(text(p.head, "polyphonic")).to_equal(std::string("poly"));
		expect(text(p.head, "voicePriority")).to_equal(std::string("1"));
		expect(text(p.head, "mode")).to_equal(std::string("subtractive"));
		deluge::test::set(s, "sideChainSend", "5");
		deluge::test::set(s, "transpose", "-12");
		deluge::test::set(s, "cents", "7");
		expect(keys(encode_pieces(s, {}).head))
		    .to_equal(std::vector<std::string>{"polyphonic", "voicePriority", "sideChainSend", "mode", "transpose",
		                                       "cents"});
	});

	it("writes the body in file order, with modulators only for FM", _ {
		SoundState s;
		expect(keys(encode_pieces(s, {}).body))
		    .to_equal(std::vector<std::string>{"osc1", "osc2", "lfo1", "lfo2", "lfo3", "lfo4", "unison"});
		s = switch_engine(s, SynthMode::FM);
		expect(keys(encode_pieces(s, {}).body))
		    .to_equal(std::vector<std::string>{"osc1", "osc2", "lfo1", "lfo2", "lfo3", "lfo4", "modulator1",
		                                       "modulator2", "unison"});
	});

	it("leaves an FM source's type out", _ {
		SoundState s;
		s = switch_engine(s, SynthMode::FM);
		const SoundPieces osc1Pieces = encode_pieces(s, {});
		const Value& osc1 = *osc1Pieces.body.find("osc1");
		expect(keys(osc1)).to_equal(std::vector<std::string>{"transpose", "cents", "retrigPhase"});
	});

	it("writes one sample range on the source itself, with its zone", _ {
		SoundState s;
		s = deluge::test::with_kind(s, SourceId::A, OscType::SAMPLE);
		deluge::test::set(s, "osc1/linearInterpolation", "0"); // smooth
		add_range(s, SourceId::A, {.file_path = "SAMPLES/A.WAV", .start_pos = 0, .end_pos = 1000});
		const SoundPieces osc1Pieces = encode_pieces(s, {});
		const Value& osc1 = *osc1Pieces.body.find("osc1");
		expect(keys(osc1)).to_equal(std::vector<std::string>{"type", "loopMode", "reversed", "timeStretchEnable",
		                                                     "timeStretchAmount", "fileName", "zone"});
		expect(keys(*osc1.find("zone"))).to_equal(std::vector<std::string>{"startSamplePos", "endSamplePos"});
	});

	it("writes no sample file for a sample source with none, and every range of several in a list", _ {
		SoundState s;
		s = deluge::test::with_kind(s, SourceId::A, OscType::SAMPLE);
		const SoundPieces nonePieces = encode_pieces(s, {});
		const Value& none = *nonePieces.body.find("osc1");
		expect(keys(none)).to_equal(std::vector<std::string>{"type", "loopMode", "reversed", "timeStretchEnable",
		                                                     "timeStretchAmount"});
		for (int16_t top : {40, 60, 127}) {
			add_range(s, SourceId::A, {.top_note = top, .file_path = "SAMPLES/A.WAV", .transpose = 2});
		}
		const SoundPieces osc1Pieces = encode_pieces(s, {});
		const Value& osc1 = *osc1Pieces.body.find("osc1");
		const Value& list = *osc1.find("sampleRanges");
		expect(list.items().size()).to_equal(3u);
		const Value& first = *list.items()[0].find("sampleRange");
		const Value& last = *list.items()[2].find("sampleRange");
		expect(keys(first)).to_equal(std::vector<std::string>{"rangeTopNote", "fileName", "transpose", "zone"});
		expect(keys(last)).to_equal(std::vector<std::string>{"fileName", "transpose", "zone"});
	});

	it("writes wavetable ranges by file alone", _ {
		SoundState s;
		s = deluge::test::with_kind(s, SourceId::B, OscType::WAVETABLE);
		s = with_ranges(std::move(s), SourceId::B, {RangeState{.file_path = "SAMPLES/W.WAV"}});
		const SoundPieces onePieces = encode_pieces(s, {});
		const Value& one = *onePieces.body.find("osc2");
		expect(keys(one)).to_equal(std::vector<std::string>{"type", "transpose", "cents", "retrigPhase", "fileName"});
		std::vector<RangeState> ranges = source_ranges(s, SourceId::B);
		ranges.push_back({.top_note = 70, .file_path = "SAMPLES/X.WAV"});
		ranges[0].top_note = 50;
		s = with_ranges(std::move(s), SourceId::B, std::move(ranges));
		const SoundPieces twoPieces = encode_pieces(s, {});
		const Value& two = *twoPieces.body.find("osc2");
		expect(keys(*two.find("wavetableRanges")->items()[0].find("wavetableRange")))
		    .to_equal(std::vector<std::string>{"rangeTopNote", "fileName"});
	});

	it("writes a DX7 patch as upper-case hex, and oscillator sync on the second source only", _ {
		SoundState s;
		s = deluge::test::with_kind(s, SourceId::A, OscType::DX7);
		DxPatchState patch;
		patch.params[0] = 0xAB;
		patch.engine_mode = 2;
		s = with_dx_patch(std::move(s), SourceId::A, patch);
		deluge::test::set(s, "osc2/oscillatorSync", "1");
		SoundPieces p = encode_pieces(s, {});
		const Value& osc1 = *p.body.find("osc1");
		expect(text(osc1, "dx7patch").substr(0, 4)).to_equal(std::string("AB00"));
		expect(text(osc1, "dx7patch").size()).to_equal(312u);
		expect(text(osc1, "dx7enginemode")).to_equal(std::string("2"));
		expect(osc1.find("oscillatorSync")).to_be_null();
		expect(text(*p.body.find("osc2"), "oscillatorSync")).to_equal(std::string("1"));
	});

	it("writes a free-running phase as -1 and sync levels relative to the tick magnitude", _ {
		SoundState s;
		deluge::test::set(s, "osc1/retrigPhase", "-1");
		deluge::test::set(s, "lfo2/syncLevel",
		                  std::to_string(std::to_underlying(deluge::plugin::SyncLevel::QUARTER)));
		SoundPieces p = encode_pieces(s, {.input_tick_magnitude = 3});
		expect(text(*p.body.find("osc1"), "retrigPhase")).to_equal(std::string("-1"));
		expect(text(*p.body.find("lfo2"), "syncLevel")).to_equal(std::string("5"));
		expect(keys(*p.body.find("lfo2"))).to_equal(std::vector<std::string>{"type", "syncLevel", "syncType"});
	});

	it("writes max voices and the MIDI output as their own pieces", _ {
		SoundState s;
		deluge::test::set(s, "maxVoices", "8");
		deluge::test::set(s, "midiOutput/channel", "3");
		SoundPieces p = encode_pieces(s, {});
		expect(text(p.max_voices, "maxVoices")).to_equal(std::string("8"));
		expect(keys(*p.midi_output.find("midiOutput"))).to_equal(std::vector<std::string>{"channel", "noteForDrum"});
	});

	it("puts the pieces together in file order, and knows its own keys", _ {
		SoundState s;
		Value whole = encode(s, {});
		expect(keys(whole)).to_equal(std::vector<std::string>{
		    "polyphonic", "voicePriority", "mode",  "modFXType", "lpfMode",    "hpfMode",
		    "filterRoute", "maxVoices",   "osc1",  "osc2",      "lfo1",       "lfo2",
		    "lfo3",        "lfo4",        "unison", "midiOutput", "delay",     "sidechain",
		    "audioCompressor", "stutter"});
		expect(is_state_key("osc1")).to_be_true();
		expect(is_state_key("maxVoices")).to_be_true();
		expect(is_state_key("sideChainSend")).to_be_true();
		expect(is_state_key("defaultParams")).to_be_false();
		expect(is_state_key("modKnobs")).to_be_false();
		expect(is_state_key("lpfMode")).to_be_true();
		expect(is_state_key("midiKnobs")).to_be_false();
	});
});

CPPSPEC_SPEC(codec_encode)
