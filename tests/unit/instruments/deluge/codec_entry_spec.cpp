#include "instruments/deluge/codec.h"
#include "serialization/tree.h"
#include "support/sound_matrix.h"

#include <cppspec.hpp>
#include <string>
#include <utility>
#include <vector>

using namespace deluge::instrument;
using deluge::serialization::Value;

namespace {

/// The host's keys, which a sound element holds and the instrument never writes.
const std::vector<std::string_view> kHostKeys{"sideChainSend", "midiOutput"};

} // namespace

// clang-format off
describe codec_entry("one instrument entry's encoding", ${
	it("encodes each entry as the whole encoding holds it", _ {
		std::vector<std::string> wrong;
		for (const int32_t magnitude : {1, 3}) {
			const deluge::plugin::CodecContext codec{.input_tick_magnitude = magnitude};
			deluge::test::for_each_named_sound([&](const std::string& name, const SoundState& state) {
				const Value whole = encode(state, codec);
				for (const std::string_view key : state_keys()) {
					const auto entry = encode_entry(state, key, codec);
					const Value* held = whole.find(key);
					if (entry.has_value() != (held != nullptr) || (held != nullptr && !(*entry == *held))) {
						wrong.push_back(name + "/" + std::string(key) + "@" + std::to_string(magnitude));
					}
				}
			});
		}
		expect(wrong).to_equal(std::vector<std::string>{});
	});

	it("writes no host entry, at any threshold", _ {
		std::vector<std::string> wrong;
		const deluge::plugin::CodecContext codec{.input_tick_magnitude = 2};
		deluge::test::for_each_named_sound([&](const std::string& name, const SoundState& state) {
			for (const std::string_view key : kHostKeys) {
				for (const std::optional<int32_t> threshold : {std::optional<int32_t>{}, std::optional<int32_t>{12345}}) {
					if (encode_entry(state, key, codec, threshold).has_value() || encode(state, codec).find(key) != nullptr) {
						wrong.push_back(name + "/" + std::string(key));
					}
				}
			}
		});
		expect(wrong).to_equal(std::vector<std::string>{});
	});

	it("encodes nothing for a key the instrument does not own", _ {
		const deluge::plugin::CodecContext codec{.input_tick_magnitude = 1};
		expect(encode_entry(SoundState{}, "defaultParams", codec).has_value()).to_be_false();
		expect(encode_entry(SoundState{}, "compressor", codec).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(codec_entry)
