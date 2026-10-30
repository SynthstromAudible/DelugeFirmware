#include "instruments/deluge/cable_codec.h"
#include "instruments/deluge/params.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <fstream>
#include <sstream>
#include <string>

using namespace deluge::instrument;
using deluge::plugin::ParamId;
using deluge::serialization::Value;

namespace {
std::vector<CableState> decodeFixture(const std::string& name, Value& doc) {
	std::ifstream in(std::string(DELUGE_INSTRUMENT_FIXTURES) + "/" + name);
	std::stringstream text;
	text << in.rdbuf();
	doc = *deluge::serialization::parseXml(text.str());
	return decode_cables(*doc.find("patchCables"),
	                     [](const Value& amount) { return deluge::serialization::hex_of(&amount).value_or(0) != 0; });
}
uint32_t depth(PatchSource source, ParamId destination) {
	return cable_depth_id(source, destination)->value;
}
} // namespace

// clang-format off
describe cable_codec("the Deluge instrument's cable decoder", ${
	it("reads cables and the cables controlling their depth, each with its own polarity", _ {
		Value doc = Value::makeMap();
		auto cables = decodeFixture("cables_current.XML", doc);
		// the nested envelope2 first (the second envelope2 repeats its source and is dropped), then lfo1
		expect(cables.size()).to_equal(size_t{4});
		expect(to_string(cables[0].source)).to_equal(std::string_view("envelope2"));
		expect(cables[0].destination.value).to_equal(depth(PatchSource::LFO_GLOBAL_1, param::kLpfFrequency));
		expect(to_string(cables[0].polarity)).to_equal(std::string_view("unipolar"));
		expect(to_string(cables[1].source)).to_equal(std::string_view("lfo1"));
		expect(cables[1].destination.value).to_equal(param::kLpfFrequency.value);
		expect(cables[1].depth().value).to_equal(depth(PatchSource::LFO_GLOBAL_1, param::kLpfFrequency));
		expect(to_string(cables[2].source)).to_equal(std::string_view("aftertouch"));
		expect(to_string(cables[2].polarity)).to_equal(std::string_view("unipolar")); // aftertouch's default
		expect(cables[3].destination.value).to_equal(param::kVolume.value);
		expect(*deluge::serialization::text_of(cables[3].amount)).to_equal(std::string("0x3FFFFFE8"));
	});

	it("reads none of the cable forms no release writes: a pre-3.2 range cable goes nowhere, rangeAdjustable is ignored", _ {
		Value doc = Value::makeMap();
		auto cables = decodeFixture("cables_pre32_range.XML", doc);
		expect(cables.size()).to_equal(size_t{3});
		expect(cables[1].destination.value).to_equal(kUnknownDestination.value);
		expect(cables[2].destination.value).to_equal(kUnknownDestination.value);
	});
	it("drops what the runtime drops, and keeps an unknown destination", _ {
		Value doc = Value::makeMap();
		auto cables = decodeFixture("cables_dropped.XML", doc);
		// x->pitch, an unknown source, a zero amount and the repeated note->pan go; lfo1->nosuchparam stays
		expect(cables.size()).to_equal(size_t{2});
		expect(cables[0].destination.value).to_equal(kUnknownDestination.value);
		expect(to_string(cables[1].source)).to_equal(std::string_view("note"));
	});

	it("keeps at most 32 cables", _ {
		Value many = Value::makeArray();
		for (size_t i = 0; i < 40; ++i) {
			Value cable = Value::makeMap();
			cable.set("source", Value::makeScalar("lfo1"));
			cable.set("destination", Value::makeScalar(std::string(kParams[i].file_name)));
			cable.set("amount", Value::makeScalar("0x10000000"));
			Value box = Value::makeMap();
			box.set("patchCable", std::move(cable));
			many.append(std::move(box));
		}
		expect(decode_cables(many, [](const Value&) { return true; }).size()).to_equal(kMaxCables);
	});
});

CPPSPEC_SPEC(cable_codec)
