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

} // namespace

// clang-format off
describe codec_entry("one instrument entry's encoding", ${
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

	it("encodes nothing for a key the instrument does not own", _ {
		const deluge::engine::CodecContext codec{.input_tick_magnitude = 1};
		expect(encode_entry(new_synth_state(), "defaultParams", codec).has_value()).to_be_false();
		expect(encode_entry(new_synth_state(), "compressor", codec).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(codec_entry)
