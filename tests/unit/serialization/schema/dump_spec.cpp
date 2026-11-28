#include "serialization/parse_json.h"
#include "serialization/schema/dump.h"
#include "support/printers/value_printer.h"
#include "test_schemas.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <array>
#include <string>
#include <vector>

using namespace deluge::serialization;
using namespace deluge::serialization::schema;
using namespace deluge::serialization::schema::test;

namespace {
const Value& field_named(const Value& record, std::string_view key) {
	for (const Value& field : record.find("fields")->items()) {
		if (field.find("key")->str() == key) {
			return field;
		}
	}
	return record;
}
} // namespace

// clang-format off
describe schema_dump("the schema dump", ${
	const std::array<const RecordDesc*, 1> roots{&kPanelDesc};
	const Value dump = dump_schema(roots, 7);

	it("records the revision and each record once, after the records it reaches", _ {
		expect(dump.find("revision")->str()).to_equal(std::string{"7"});
		std::vector<std::string> names;
		for (const Value& record : dump.find("records")->items()) {
			names.push_back(record.find("name")->str());
		}
		expect(names).to_equal(std::vector<std::string>{"point", "knob", "label", "circle", "square", "panel"});
	});

	it("describes each shape", _ {
		const Value& panel = dump.find("records")->items().back();
		expect(panel.find("open")->str()).to_equal(std::string{"true"});
		expect(field_named(panel, "level")).to_equal(*parseJson(
		    R"({"key": "level", "kind": "scalar", "form": "integer", "min": -10, "max": 10})"));
		expect(field_named(panel, "colour")).to_equal(*parseJson(
		    R"({"key": "colour", "kind": "scalar", "form": "word", "words": ["red", "green", "blue"]})"));
		expect(field_named(panel, "pair")).to_equal(*parseJson(
		    R"({"key": "pair", "kind": "list", "capacity": 2, "element": {"kind": "scalar", "form": "integer", "min": -5, "max": 5}})"));
		expect(field_named(panel, "labels")).to_equal(*parseJson(
		    R"({"key": "labels", "kind": "list", "idKey": "cc", "element": {"kind": "record", "record": "label"}})"));
		expect(field_named(panel, "figure")).to_equal(*parseJson(
		    R"({"key": "figure", "kind": "variant", "alternatives": [{"word": "circle", "record": "circle"}, {"word": "square", "record": "square"}]})"));
		expect(field_named(panel, "plugin")).to_equal(*parseJson(
		    R"({"key": "plugin", "kind": "plugin", "plugin": "test.plugin", "version": 1})"));
	});
});
// clang-format on

int dump_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(schema_dump).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
