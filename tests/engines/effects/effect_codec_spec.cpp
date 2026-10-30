#include "effect_test_states.h"
#include "engines/effects/codec.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <ostream>
#include <string>

using namespace deluge::effects;
using deluge::serialization::Value;

using namespace deluge::effects::testing;

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
		constexpr deluge::plugin::CodecContext kAtThree{.input_tick_magnitude = 3};
		EffectStates read;
		decode(encode(busy(), kAtThree), read, kAtThree);
		expect(read.delay.sync_level).to_equal(busy().delay.sync_level);
		expect(encode(busy(), kAtThree).find("delay")->find("syncLevel")->str())
		    .not_().to_equal(encode(busy(), kAtOne).find("delay")->find("syncLevel")->str());
	});
	it("writes that a stutter keeps its own settings, and nothing when it uses the song's", _ {
		EffectStates own = busy();
		own.stutter.use_song_stutter = false;
		expect(encode(own, kAtOne).find("stutter")->find("useSongStutter")->str()).to_equal(std::string("0"));
		expect(encode(busy(), kAtOne).find("stutter")->find("useSongStutter")).to_be_null();
		EffectStates read;
		decode(encode(own, kAtOne), read, kAtOne);
		expect(read.stutter.use_song_stutter).to_be_false();
	});
	it("reads an older stutter tag as using the song's", _ {
		auto doc = deluge::serialization::parseXml(R"(<sound><stutter quantized="1" reverse="0" pingPong="0"/></sound>)");
		EffectStates read = busy();
		read.stutter.use_song_stutter = false;
		decode(*doc->find("sound"), read, kAtOne);
		expect(read.stutter.use_song_stutter).to_be_true();
	});
	it("reads a factory preset's compressor tag as the sidechain", _ {
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
		expect(read.delay.sync_level).to_equal(deluge::plugin::SyncLevel::NONE);
		expect(read.delay.sync_type).to_equal(deluge::plugin::SyncType::EVEN);
		expect(read.delay.ping_pong).to_be_false();
	});
	it("reads a delay's type from a file that saves only whether it is analog", _ {
		EffectStates read;
		decode(*deluge::serialization::parseXml(R"(<sound><delay analog="1"/></sound>)")->find("sound"), read, kAtOne);
		expect(read.delay.type).to_equal(DelayType::ANALOG);
		decode(*deluge::serialization::parseXml(R"(<sound><delay analog="0"/></sound>)")->find("sound"), read, kAtOne);
		expect(read.delay.type).to_equal(DelayType::DIGITAL);
		read.delay.type = DelayType::OFF;
		decode(*deluge::serialization::parseXml(R"(<sound><delay pingPong="0"/></sound>)")->find("sound"), read, kAtOne);
		expect(read.delay.type).to_equal(DelayType::DIGITAL);
	});
	it("reads a saved delay type ahead of an analog flag", _ {
		EffectStates read;
		decode(*deluge::serialization::parseXml(R"(<sound><delay type="off" analog="1"/></sound>)")->find("sound"), read,
		       kAtOne);
		expect(read.delay.type).to_equal(DelayType::OFF);
		decode(*deluge::serialization::parseXml(R"(<sound><delay analog="0" type="analog"/></sound>)")->find("sound"),
		       read, kAtOne);
		expect(read.delay.type).to_equal(DelayType::ANALOG);
	});
	it("writes a delay's type and no analog flag", _ {
		const Value written = encode(busy(), kAtOne);
		expect(written.find("delay")->find("type")->str()).to_equal(std::string("analog"));
		expect(written.find("delay")->find("analog")).to_be_null();
	});
	it("tells whether an element saves its delay's type", _ {
		expect(saves_delay_type(encode(busy(), kAtOne))).to_be_true();
		expect(saves_delay_type(*deluge::serialization::parseXml(R"(<sound><delay analog="1"/></sound>)")->find("sound")))
		    .to_be_false();
		expect(saves_delay_type(*deluge::serialization::parseXml(R"(<sound lpfMode="12dB"/>)")->find("sound")))
		    .to_be_false();
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
