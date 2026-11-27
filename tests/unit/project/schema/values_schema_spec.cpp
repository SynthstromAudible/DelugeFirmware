#include "project/schema/values_schema.h"
#include "serialization/parse_json.h"
#include "serialization/schema/codec.h"
#include "serialization/write_json.h"
#include "support/printers/clip_values_printer.h"
#include "support/printers/optional_printer.h"
#include "support/printers/schema_report_printer.h"
#include "support/printers/sound_routing_printer.h"
#include "support/schema/generator.h"
#include "support/schema/round_trip.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <string>
#include <vector>

using namespace deluge::project;
using namespace deluge::project::schema;
using deluge::serialization::parseJson;
using deluge::serialization::write_json;
namespace sch = deluge::serialization::schema;

// clang-format off
describe values_schema("clip values and sound routing", ${
	it("round-trip clip values, an arp among them", _ {
		ClipValues values;
		values.midi_pgm = 12;
		values.bend_range_mpe = 48;
		values.arp.emplace().num_octaves = 3;
		const auto back = sch::decode(kClipValuesSchema, *parseJson(write_json(sch::encode(kClipValuesSchema, values))));
		expect(back->value).to_equal(values);
		expect(back->report.notes.size()).to_equal(std::size_t{0});
	});

	it("answer a clip value's path from the same table", _ {
		ClipValues values;
		values.bend_range = 7;
		expect(value_at(values, "bendRange")).to_equal(&values.bend_range);
		expect(value_at(values, "midiPGM")).to_equal(&values.midi_pgm);
		expect(value_at(values, "arp")).to_be_null();
		expect(resolve(values, "bendRange", 2)).to_equal(7);
		expect(resolve(values, "bendRangeMPE", 48)).to_equal(48);
	});

	it("clamp a routing's MIDI channel to the sixteen there are", _ {
		const auto back = sch::decode(kSoundRoutingSchema, *parseJson(R"({"sideChainSend": 5, "midiChannel": 16, "midiNote": null})"));
		expect(back->value.midi_channel).to_equal(std::optional<uint8_t>{15});
		expect(back->value.midi_note).to_equal(std::optional<uint8_t>{});
		expect(back->report.notes).to_equal(std::vector<sch::LoadNote>{{sch::LoadNote::Kind::CLAMPED, "midiChannel"}});
	});

	it("round-trip generated records", _ {
		for (uint32_t seed = 1; seed <= 200; ++seed) {
			expect(sch::test::round_trip(kClipValuesSchema, sch::test::Generator{seed}.record(kClipValuesDesc))
			           .error_or(std::string{})).to_equal(std::string{});
			expect(sch::test::round_trip(kSoundRoutingSchema, sch::test::Generator{seed}.record(kSoundRoutingDesc))
			           .error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int values_schema_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(values_schema).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
