#include "project/schema/arp_schema.h"
#include "serialization/parse_json.h"
#include "serialization/schema/codec.h"
#include "serialization/schema/lookup.h"
#include "serialization/write_json.h"
#include "support/printers/arp_record_printer.h"
#include "support/printers/schema_report_printer.h"
#include "support/printers/value_printer.h"
#include "support/schema/generator.h"
#include "support/schema/round_trip.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <string>
#include <vector>

using namespace deluge::project;
using namespace deluge::project::schema;
using deluge::serialization::parseJson;
using deluge::serialization::Value;
using deluge::serialization::write_json;
namespace sch = deluge::serialization::schema;

namespace {
ArpRecord full_arp() {
	ArpRecord arp;
	arp.mode = 1;
	arp.octave_mode = 2;
	arp.note_mode = 4;
	arp.mpe_velocity = 1;
	arp.num_octaves = 5;
	arp.step_repeat = 3;
	arp.chord_type = 2;
	arp.sync_level = 7;
	arp.sync_type = 10;
	arp.randomizer_lock = true;
	arp.kit_arp = false;
	set_host_value(arp, "arpeggiator/gate", 1234);
	set_host_value(arp, "arpeggiator/noteProbability", -1);
	return arp;
}
std::vector<std::string> keys(const Value& map) {
	std::vector<std::string> found;
	for (const auto& [key, value] : map.entries()) {
		found.push_back(key);
	}
	return found;
}
} // namespace

// clang-format off
describe arp_schema("the arp's schema", ${
	it("round-trips every setting and host value through JSON", _ {
		const ArpRecord arp = full_arp();
		const auto back = sch::decode(kArpSchema, *parseJson(write_json(sch::encode(kArpSchema, arp))));
		expect(back).to_have_value();
		expect(back->value).to_equal(arp);
		expect(back->report.notes.size()).to_equal(std::size_t{0});
	});

	it("writes the settings in row order, then every host value, null where none is held", _ {
		const Value written = sch::encode(kArpSchema, full_arp());
		expect(keys(written)).to_equal(std::vector<std::string>{"arpMode", "octaveMode", "noteMode", "mpeVelocity",
		    "numOctaves", "stepRepeat", "chordType", "syncLevel", "syncType", "randomizerLock", "kitArp", "gate", "rate",
		    "rhythm", "sequenceLength", "chordPolyphony", "ratchetAmount", "noteProbability", "bassProbability",
		    "swapProbability", "glideProbability", "reverseProbability", "chordProbability", "ratchetProbability",
		    "spreadVelocity", "spreadGate", "spreadOctave"});
		expect(written.find("arpMode")->str()).to_equal(std::string{"arp"});
		expect(written.find("syncType")->str()).to_equal(std::string{"10"});
		expect(written.find("kitArp")->str()).to_equal(std::string{"false"});
		expect(written.find("gate")->str()).to_equal(std::string{"1234"});
		expect(written.find("rate")->str()).to_equal(std::string{"null"});
	});

	it("clamps a setting outside its row's range and reads an unknown word as the first", _ {
		const auto back = sch::decode(kArpSchema, *parseJson(R"({"numOctaves": 0, "noteMode": "sideways"})"));
		expect(back->value.num_octaves).to_equal(uint8_t{1});
		expect(back->value.note_mode).to_equal(uint8_t{0});
		expect(back->report.notes).to_equal(std::vector<sch::LoadNote>{
		    {sch::LoadNote::Kind::CLAMPED, "numOctaves"}, {sch::LoadNote::Kind::UNKNOWN_WORD, "noteMode"}});
	});

	it("finds a setting's member by its key", _ {
		ArpRecord arp;
		expect(sch::member_at<uint8_t>(kArpSchema, arp, "numOctaves")).to_equal(&arp.num_octaves);
		expect(sch::member_at<uint8_t>(kArpSchema, arp, "syncType")).to_equal(&arp.sync_type);
		expect(sch::member_at<bool>(kArpSchema, arp, "kitArp")).to_equal(&arp.kit_arp);
		expect(sch::member_at<uint8_t>(kArpSchema, arp, "preset")).to_be_null();
		expect(sch::member_at<uint8_t>(kArpSchema, arp, "gate")).to_be_null();
	});

	it("round-trips generated arps", _ {
		for (uint32_t seed = 1; seed <= 200; ++seed) {
			const auto checked = sch::test::round_trip(kArpSchema, sch::test::Generator{seed}.record(kArpDesc));
			expect(checked.error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int arp_schema_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(arp_schema).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
