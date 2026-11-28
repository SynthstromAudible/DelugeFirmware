#include "project/schema/mapping_schema.h"
#include "serialization/parse_json.h"
#include "serialization/schema/codec.h"
#include "serialization/write_json.h"
#include "support/printers/mapping_printer.h"
#include "support/printers/schema_report_printer.h"
#include "support/schema/generator.h"
#include "support/schema/round_trip.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <array>
#include <string>

using namespace deluge::project;
using namespace deluge::project::schema;
using deluge::serialization::parseJson;
using deluge::serialization::write_json;
namespace sch = deluge::serialization::schema;

namespace {
/// A record holding only a mapping table.
struct Mappings {
	MappingTable table;
	bool operator==(const Mappings&) const = default;
};
inline constexpr std::array kMappingsFields{
    sch::list_with("mappings", sch::kMemberAccess<&Mappings::table>, kMappingListOps, kMappingItem)};
inline constexpr sch::RecordDesc kMappingsDesc = sch::closed_record("mappings", kMappingsFields);
inline constexpr sch::Schema<Mappings> kMappingsSchema{&kMappingsDesc};

Mapping knob(ControlsAddress owner, uint32_t param, uint8_t cc, bool relative) {
	return {.source = MidiCommand{.channel_or_zone = 2, .note_or_cc = cc},
	        .target = OwnerParamTarget{owner, ParamTarget{ParamSpace::ENGINE, deluge::plugin::ParamId{param}}},
	        .relative = relative};
}
} // namespace

// clang-format off
describe mapping_schema("the mapping table's schema", ${
	it("round-trips a mapping of every target, as written, in order", _ {
		Mappings m;
		m.table.restore(knob({ControlsAddress::Kind::OUTPUT, 3}, 17, 74, false));
		m.table.restore(knob({ControlsAddress::Kind::SONG, 0}, 2, 74, true));
		m.table.restore({.source = MidiCommand{.channel_or_zone = 16, .note_or_cc = 60,
		                     .device = MidiDeviceRef{.port = MidiDeviceRef::Port::HOSTED, .name = "Keys", .vendor_id = 0x1234, .product_id = 7}},
		                 .target = ClipMuteTarget{9}});
		m.table.restore({.source = MidiCommand{.channel_or_zone = 1, .note_or_cc = 36}, .target = DrumMuteTarget{5}});
		m.table.restore({.source = MidiCommand{.channel_or_zone = 1, .note_or_cc = 40}, .target = SectionLaunchTarget{2}});
		m.table.restore({.source = MidiCommand{.channel_or_zone = 4}, .target = TrackInputTarget{3}});
		m.table.restore({.source = MidiCommand{.channel_or_zone = 5}, .target = LaneInputTarget{5}});
		const auto back = sch::decode(kMappingsSchema, *parseJson(write_json(sch::encode(kMappingsSchema, m))));
		expect(back).to_have_value();
		expect(back->value).to_equal(m);
		expect(back->report.notes.size()).to_equal(std::size_t{0});
	});

	it("writes a target as a record of its kind", _ {
		Mappings m;
		m.table.restore(knob({ControlsAddress::Kind::DRUM, 8}, 40, 1, true));
		const auto written = sch::encode(kMappingsSchema, m);
		const auto& target = *written.find("mappings")->items()[0].find("target");
		expect(write_json(target)).to_equal(write_json(*parseJson(
		    R"({"kind": "param", "ownerKind": "drum", "ownerId": 8, "param": {"space": "engine", "id": 40}})")));
	});

	it("drops an unknown key of a target, which is an identity", _ {
		const auto back = sch::decode(kMappingsSchema, *parseJson(R"({"mappings": [{"source": {"channelOrZone": 1,
		    "noteOrCc": 2, "device": null}, "target": {"kind": "clipMute", "clip": 4, "colour": 1}, "relative": false}]})"));
		expect(back->report.notes).to_equal(std::vector<sch::LoadNote>{
		    {sch::LoadNote::Kind::UNKNOWN_KEY_DROPPED, "mappings/0/target/colour"}});
	});

	it("round-trips generated tables", _ {
		for (uint32_t seed = 1; seed <= 200; ++seed) {
			const auto checked = sch::test::round_trip(kMappingsSchema, sch::test::Generator{seed}.record(kMappingsDesc));
			expect(checked.error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int mapping_schema_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(mapping_schema).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
