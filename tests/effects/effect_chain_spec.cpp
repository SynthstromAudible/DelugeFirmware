#include "effect_test_states.h"
#include "effects/chain.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <string>
#include <string_view>
#include <vector>

using namespace deluge::effects;
using namespace deluge::effects::testing;
namespace serialization = deluge::serialization;

// clang-format off
describe effect_chain("each home's chain of effect slots", ${
	it("lays a clip chain out in its processing order", _ {
		const auto ids = chain_effects(Home::CLIP);
		expect(std::vector<std::string_view>(ids.begin(), ids.end()))
		    .to_equal(std::vector<std::string_view>{"saturation", "filter", "bitcrush", "mod_fx", "eq", "delay",
		                                           "stutter", "compressor", "sidechain"});
		expect(reverb_return(Home::CLIP)).not_().to_have_value();
	});
	it("splits the master at the reverb returns", _ {
		const auto ids = chain_effects(Home::MASTER);
		expect(std::vector<std::string_view>(ids.begin(), ids.end()))
		    .to_equal(std::vector<std::string_view>{"mod_fx", "eq", "delay", "filter", "bitcrush", "stutter",
		                                           "compressor", "saturation", "sidechain"});
		expect(*reverb_return(Home::MASTER)).to_equal(uint32_t{3});
	});
	it("gives each effect the entries it owns, and the slots together the owner's whole encoding", _ {
		const EffectStates s = busy();
		const auto slots = encode_chain(s, Home::CLIP, kAtOne);
		expect(slots[1].entries.find("lpfMode") != nullptr).to_be_true();
		expect(slots[5].entries.find("delay") != nullptr).to_be_true();
		auto element = serialization::Value::makeMap();
		write_chain(slots, serialization::Value::makeMap(), element);
		expect(serialization::first_difference(element, encode(s, kAtOne))).to_equal(std::string{});
	});
	it("reads each slot back onto its effect alone", _ {
		const EffectStates s = busy();
		EffectStates read;
		for (const SlotEntries& slot : encode_chain(s, Home::CLIP, kAtOne)) {
			decode_slot(slot.effect, slot.entries, read, kAtOne);
		}
		expect(read).to_equal(s);
	});
	it("names the effect each setting belongs to", _ {
		expect(*effect_of_setting("lpfMode")).to_equal(std::string_view("filter"));
		expect(*effect_of_setting("delay/syncLevel")).to_equal(std::string_view("delay"));
		expect(*effect_of_setting("audioCompressor/thresh")).to_equal(std::string_view("compressor"));
		expect(*effect_of_setting("clippingAmount")).to_equal(std::string_view("saturation"));
		expect(effect_of_setting("no/such")).not_().to_have_value();
	});
	it("keeps a host owner's view entries apart from its effects", _ {
		auto element = serialization::parseXml(R"(<kit modFXCurrentParam="depth" currentFilterType="hpf"/>)");
		const ChainView view = read_view(*element->find("kit"));
		expect(static_cast<int>(view.mod_fx_param)).to_equal(0);
		expect(static_cast<int>(view.filter_type)).to_equal(1);
	});
	it("writes every view it reads, as the words a file holds", _ {
		for (uint8_t param = 0; param < kModFxParamWords.size(); ++param) {
			for (uint8_t filter = 0; filter < kFilterTypeWords.size(); ++filter) {
				const ChainView view{.mod_fx_param = param, .filter_type = filter};
				const serialization::Value words = view_value(view);
				expect(words.find("modFXCurrentParam")->str()).to_equal(std::string(kModFxParamWords[param]));
				expect(words.find("currentFilterType")->str()).to_equal(std::string(kFilterTypeWords[filter]));
				expect(read_view(words) == view).to_be_true();
			}
		}
	});
	it("reads an owner without view entries as the default view, and an unknown word as the app's parse does", _ {
		auto bare = serialization::parseXml(R"(<kit/>)");
		expect(read_view(*bare->find("kit")) == ChainView{}).to_be_true();
		auto odd = serialization::parseXml(R"(<kit modFXCurrentParam="wobble" currentFilterType="notch"/>)");
		const ChainView view = read_view(*odd->find("kit"));
		expect(static_cast<int>(view.mod_fx_param)).to_equal(2); // offset
		expect(static_cast<int>(view.filter_type)).to_equal(0);  // lpf
	});
});

CPPSPEC_SPEC(effect_chain)
