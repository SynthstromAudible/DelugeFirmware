#include "engines/effects/codec.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <ostream>
#include <string>

using namespace deluge::effects;
using deluge::serialization::Value;

namespace {
constexpr deluge::engine::CodecContext kAtOne{.input_tick_magnitude = 1};

EffectStates busy() {
	EffectStates s;
	s.filter = {.lpf_mode = FilterMode::SVF_NOTCH, .hpf_mode = FilterMode::SVF_BAND, .route = FilterRoute::PARALLEL};
	s.saturation.clipping_amount = 3;
	s.mod_fx.type = ModFXType::GRAIN;
	s.delay = {.ping_pong = false,
	           .analog = true,
	           .sync_type = deluge::engine::SyncType::TRIPLET,
	           .sync_level = deluge::engine::SyncLevel::EIGHTH};
	s.stutter = {.use_song_stutter = true, .quantized = false, .reversed = true, .ping_pong = true};
	s.compressor = {.attack = 1 << 24,
	                .release = 2 << 24,
	                .threshold = 3 << 24,
	                .ratio = 4 << 24,
	                .sidechain_hpf = 5 << 24,
	                .blend = 6 << 24};
	s.sidechain = {.attack = 123,
	               .release = 456,
	               .sync_type = deluge::engine::SyncType::DOTTED,
	               .sync_level = deluge::engine::SyncLevel::QUARTER};
	return s;
}
} // namespace

namespace deluge::effects {
/// States, printed as their encoding, so a failing comparison shows them.
std::ostream& operator<<(std::ostream& out, const EffectStates& states) {
	return out << deluge::serialization::first_difference(encode(states, kAtOne), Value::makeMap());
}

/// A delay's state, printed field by field.
std::ostream& operator<<(std::ostream& out, const DelayState& delay) {
	return out << "{pingPong " << delay.ping_pong << " analog " << delay.analog << " syncType "
	           << static_cast<int>(delay.sync_type) << " syncLevel " << static_cast<int>(delay.sync_level) << "}";
}
} // namespace deluge::effects

// clang-format off
describe effect_codec("an owner element's effect entries", ${
	it("reads back every setting it writes", _ {
		EffectStates read;
		decode(encode(busy(), kAtOne), read, kAtOne);
		expect(read).to_equal(busy());
	});
	it("writes the entries in a file's order, the delay apart from the rest", _ {
		const EffectPieces pieces = encode_pieces(busy(), kAtOne);
		std::string keys;
		for (const Value* piece : {&pieces.attributes, &pieces.delay, &pieces.tags}) {
			for (const auto& [key, value] : piece->entries()) {
				keys += key + ",";
			}
		}
		expect(keys).to_equal(std::string("modFXType,lpfMode,hpfMode,filterRoute,clippingAmount,delay,sidechain,"
		                                  "audioCompressor,stutter,"));
	});
	it("leaves out a clipping amount of zero", _ {
		EffectStates quiet = busy();
		quiet.saturation.clipping_amount = 0;
		expect(encode(quiet, kAtOne).find("clippingAmount")).to_be_null();
	});
	it("writes the compressor's threshold it is given in place of the state's", _ {
		const EffectPieces pieces = encode_pieces(busy(), kAtOne, 77);
		expect(pieces.tags.find("audioCompressor")->find("thresh")->str()).to_equal(std::string("77"));
	});
	it("writes sync levels relative to the song's tick magnitude", _ {
		constexpr deluge::engine::CodecContext kAtThree{.input_tick_magnitude = 3};
		EffectStates read;
		decode(encode(busy(), kAtThree), read, kAtThree);
		expect(read.delay.sync_level).to_equal(busy().delay.sync_level);
		expect(encode(busy(), kAtThree).find("delay")->find("syncLevel")->str())
		    .not_().to_equal(encode(busy(), kAtOne).find("delay")->find("syncLevel")->str());
	});
	it("never writes whether a stutter uses the song's, and reads every stutter tag as using it", _ {
		EffectStates own = busy();
		own.stutter.use_song_stutter = false;
		expect(encode(own, kAtOne).find("stutter")->find("useSongStutter")).to_be_null();
		EffectStates read = own;
		decode(encode(own, kAtOne), read, kAtOne);
		expect(read.stutter.use_song_stutter).to_be_true();
	});
	it("reads an old compressor tag as the sidechain", _ {
		auto doc = deluge::serialization::parseXml(R"(<sound><compressor attack="11" release="22" syncLevel="0"/></sound>)");
		EffectStates read;
		decode(*doc->find("sound"), read, kAtOne);
		expect(read.sidechain.attack).to_equal(11);
		expect(read.sidechain.release).to_equal(22);
	});
	it("sets a delay's sync to none and even before reading a delay tag, as the runtime reader does", _ {
		auto doc = deluge::serialization::parseXml(R"(<sound><delay pingPong="0" feedback="5"/></sound>)");
		EffectStates read = busy();
		decode(*doc->find("sound"), read, kAtOne);
		expect(read.delay.sync_level).to_equal(deluge::engine::SyncLevel::NONE);
		expect(read.delay.sync_type).to_equal(deluge::engine::SyncType::EVEN);
		expect(read.delay.ping_pong).to_be_false();
	});
	it("keeps what an element does not name", _ {
		auto doc = deluge::serialization::parseXml(R"(<sound lpfMode="12dB"/>)");
		EffectStates read = busy();
		decode(*doc->find("sound"), read, kAtOne);
		expect(read.filter.lpf_mode).to_equal(FilterMode::TRANSISTOR_12DB);
		expect(read.delay).to_equal(busy().delay);
	});
});

CPPSPEC_SPEC(effect_codec)
