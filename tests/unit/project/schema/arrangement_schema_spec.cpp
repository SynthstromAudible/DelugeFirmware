#include "project/schema/arrangement_schema.h"
#include "serialization/parse_json.h"
#include "serialization/schema/codec.h"
#include "serialization/write_json.h"
#include "support/printers/schema_report_printer.h"
#include "support/printers/song_settings_printer.h"
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
describe arrangement_schema("the arrangement's schema", ${
	it("writes every section and every macro, and reads them back", _ {
		Arrangement a;
		a.sections[0].num_repetitions = 3;
		a.sections[23].num_repetitions = -2;
		a.session_macros[0] = {.kind = SessionMacroKind::CLIP_LAUNCH, .clip_id = 11};
		a.session_macros[7] = {.kind = SessionMacroKind::SECTION, .section = 5};
		const auto written = sch::encode(kArrangementSchema, a);
		expect(written.find("sections")->items().size()).to_equal(std::size_t{24});
		expect(written.find("sessionMacros")->items().size()).to_equal(std::size_t{8});
		expect(written.find("sessionMacros")->items()[0].find("kind")->str()).to_equal(std::string{"clipLaunch"});
		const auto back = sch::decode(kArrangementSchema, *parseJson(write_json(written)));
		expect(back->value).to_equal(a);
	});

	it("drops sections past the 24th, and keeps the defaults of those the document lacks", _ {
		std::string sections = R"({"sections": [)";
		for (int i = 0; i < 25; ++i) {
			sections += (i == 0 ? "" : ", ") + std::string{R"({"numRepetitions": 1})"};
		}
		sections += "]}";
		const auto back = sch::decode(kArrangementSchema, *parseJson(sections));
		expect(back->report.notes).to_equal(std::vector<sch::LoadNote>{{sch::LoadNote::Kind::TOO_MANY_ITEMS, "sections/24"}});
		const auto few = sch::decode(kArrangementSchema, *parseJson(R"({"sections": [{"numRepetitions": 4}]})"));
		expect(few->value.sections[0].num_repetitions).to_equal(int16_t{4});
		expect(few->value.sections[1].num_repetitions).to_equal(int16_t{0});
	});

	it("clamps a section's repeats to the range the firmware plays", _ {
		const auto back = sch::decode(kArrangementSchema, *parseJson(R"({"sections": [{"numRepetitions": 20000}]})"));
		expect(back->value.sections[0].num_repetitions).to_equal(int16_t{9999});
	});

	it("round-trips generated arrangements", _ {
		for (uint32_t seed = 1; seed <= 200; ++seed) {
			const auto checked = sch::test::round_trip(kArrangementSchema, sch::test::Generator{seed}.record(kArrangementDesc));
			expect(checked.error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int arrangement_schema_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(arrangement_schema).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
