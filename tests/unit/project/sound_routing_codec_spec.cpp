#include "instruments/deluge/codec.h"
#include "project/sound_routing_codec.h"
#include "serialization/tree.h"
#include "support/printers/sound_routing_printer.h"
#include "support/record_state.h"

#include <cppspec.hpp>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using namespace deluge::project;
using deluge::serialization::Value;
namespace instrument = deluge::instrument;

namespace {

/// Routings across what the file writes: none, a send alone, channel and note at their least and past the menus'.
std::vector<SoundRouting> routings() {
	return {SoundRouting{}, SoundRouting{.side_chain_send = 123456},
	        SoundRouting{.side_chain_send = INT32_MAX, .midi_channel = 0, .midi_note = 0},
	        SoundRouting{.midi_channel = 15, .midi_note = 127}, SoundRouting{.midi_channel = 254, .midi_note = 200}};
}

/// Sounds whose pieces differ around the routing's entries: a new synth, an FM sound and a sample sound.
std::vector<instrument::SoundState> sounds() {
	instrument::SoundState fm = deluge::test::with(instrument::SoundState{}, "mode", "fm");
	instrument::SoundState sample =
	    deluge::test::with_kind(instrument::SoundState{}, instrument::SourceId::A, instrument::OscType::SAMPLE);
	return {instrument::SoundState{}, fm, sample};
}

/// Where @p a's pieces and @p b's first differ, or empty.
std::string pieces_difference(const instrument::SoundPieces& a, const instrument::SoundPieces& b) {
	using deluge::serialization::first_difference;
	for (const auto& [name, pa, pb] :
	     {std::tuple{"head", &a.head, &b.head},
	      std::tuple{"effect_attributes", &a.effect_attributes, &b.effect_attributes},
	      std::tuple{"max_voices", &a.max_voices, &b.max_voices}, std::tuple{"body", &a.body, &b.body},
	      std::tuple{"midi_output", &a.midi_output, &b.midi_output}, std::tuple{"delay", &a.delay, &b.delay},
	      std::tuple{"effect_tags", &a.effect_tags, &b.effect_tags}}) {
		if (std::string d = first_difference(*pa, *pb); !d.empty()) {
			return std::string{name} + " at " + d;
		}
	}
	return {};
}

} // namespace

// clang-format off
describe sound_routing_codec("a sound's routing in its element", ${
	it("writes a new sound's routing as no send and a MIDI output of 255 and 255", _ {
		const Value entries = encode_routing(SoundRouting{});
		expect(entries.find("sideChainSend")).to_be_null();
		expect(entries.find("midiOutput")->find("channel")->str()).to_equal(std::string{"255"});
		expect(entries.find("midiOutput")->find("noteForDrum")->str()).to_equal(std::string{"255"});
	});

	it("reads back every routing it writes", _ {
		for (const SoundRouting& routing : routings()) {
			expect(decode_routing(encode_routing(routing))).to_equal(routing);
		}
	});

	it("reads an element that holds neither entry as no send and no MIDI output", _ {
		Value element = Value::makeMap();
		element.set("mode", Value::makeScalar("fm"));
		expect(decode_routing(element)).to_equal(SoundRouting{});
	});

	it("is placed by the instrument in the file's order: the send in the head after the voice priority, the MIDI output as its own piece", _ {
		for (const instrument::SoundState& sound : sounds()) {
			const instrument::SoundPieces own = instrument::encode_pieces(sound, {});
			for (const SoundRouting& routing : routings()) {
				const Value host = encode_routing(routing);
				const instrument::SoundPieces placed = instrument::encode_pieces(sound, {}, std::nullopt, &host);
				Value head = Value::makeMap();
				for (const auto& [key, value] : own.head.entries()) {
					head.set(key, value);
					if (key == "voicePriority" && routing.side_chain_send != 0) {
						head.set("sideChainSend", *host.find("sideChainSend"));
					}
				}
				Value midi_output = Value::makeMap();
				midi_output.set("midiOutput", *host.find("midiOutput"));
				instrument::SoundPieces want = own;
				want.head = head;
				want.midi_output = midi_output;
				expect(pieces_difference(placed, want)).to_equal(std::string{});
			}
		}
	});
});
// clang-format on

CPPSPEC_SPEC(sound_routing_codec)
