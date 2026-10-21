#include "engines/deluge/codec.h"
#include "serialization/tree.h"
#include "support/state_matrix.h"

#include <cppspec.hpp>
#include <string>
#include <utility>
#include <vector>

using namespace deluge::instrument;
using deluge::instrument::test::matrix;
using deluge::serialization::Value;

namespace {

/// The top-level keys encode writes, in file order (codec.cpp's kStateKeys).
const std::vector<std::string_view> kKeys{
    "polyphonic",     "voicePriority", "sideChainSend", "mode",       "transpose",
    "cents",          "modFXType",     "lpfMode",       "hpfMode",    "filterRoute",
    "clippingAmount", "maxVoices",     "osc1",          "osc2",       "lfo1",
    "lfo2",           "lfo3",          "lfo4",          "modulator1", "modulator2",
    "unison",         "midiOutput",    "delay",         "sidechain",  "audioCompressor",
    "stutter"};

/// A slot around the instrument's entries: the sound's params before them and its arpeggiator after.
Value slot_of(const SoundState& state, const deluge::engine::CodecContext& codec) {
	Value base = Value::makeMap();
	Value params = Value::makeMap();
	params.set("volume", Value::makeScalar("0x00000000"));
	base.set("defaultParams", std::move(params));
	Value arp = Value::makeMap();
	arp.set("mode", Value::makeScalar("off"));
	base.set("arpeggiator", std::move(arp));
	return with_state(base, state, codec);
}

/// The keys whose encoding differs between @p a and @p b, present in one alone included, in file order.
std::vector<std::string_view> differing(const SoundState& a, const SoundState& b,
                                        const deluge::engine::CodecContext& codec) {
	const Value ea = encode(a, codec);
	const Value eb = encode(b, codec);
	std::vector<std::string_view> keys;
	for (const std::string_view key : kKeys) {
		const Value* va = ea.find(key);
		const Value* vb = eb.find(key);
		if ((va == nullptr) != (vb == nullptr) || (va != nullptr && !(*va == *vb))) {
			keys.push_back(key);
		}
	}
	return keys;
}

} // namespace

// clang-format off
describe codec_entry("one instrument entry's encoding, and a slot's entries replaced in place", ${
	it("encodes each entry as the whole encoding holds it", _ {
		std::vector<std::string> wrong;
		for (const int32_t magnitude : {1, 3}) {
			const deluge::engine::CodecContext codec{.input_tick_magnitude = magnitude};
			for (const auto& [name, state] : matrix()) {
				const Value whole = encode(state, codec);
				for (const std::string_view key : kKeys) {
					const auto entry = encode_entry(state, key, codec);
					const Value* held = whole.find(key);
					if (entry.has_value() != (held != nullptr) || (held != nullptr && !(*entry == *held))) {
						wrong.push_back(name + "/" + std::string(key) + "@" + std::to_string(magnitude));
					}
				}
			}
		}
		expect(wrong).to_equal(std::vector<std::string>{});
	});

	it("replaces only the entries asked, as with_state replaces them all", _ {
		std::vector<std::string> wrong;
		for (const int32_t magnitude : {1, 3}) {
			const deluge::engine::CodecContext codec{.input_tick_magnitude = magnitude};
			const auto states = matrix();
			for (const auto& [from_name, from] : states) {
				for (const auto& [to_name, to] : states) {
					Value slot = slot_of(from, codec);
					with_entries(slot, to, differing(from, to, codec), codec);
					if (!(slot == with_state(slot_of(from, codec), to, codec))) {
						wrong.push_back(from_name + "->" + to_name + "@" + std::to_string(magnitude));
					}
				}
			}
		}
		expect(wrong).to_equal(std::vector<std::string>{});
	});

	it("replaces every entry as with_state does when asked for all", _ {
		const deluge::engine::CodecContext codec{.input_tick_magnitude = 1};
		for (const auto& [name, state] : matrix()) {
			Value slot = slot_of(new_synth_state(), codec);
			with_entries(slot, state, kKeys, codec);
			expect(slot == with_state(slot_of(new_synth_state(), codec), state, codec)).to_be_true();
		}
	});

	it("encodes nothing for a key the instrument does not own", _ {
		const deluge::engine::CodecContext codec{.input_tick_magnitude = 1};
		expect(encode_entry(new_synth_state(), "defaultParams", codec).has_value()).to_be_false();
		expect(encode_entry(new_synth_state(), "compressor", codec).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(codec_entry)
