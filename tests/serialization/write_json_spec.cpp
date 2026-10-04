#include "serialization/parse_json.h"
#include "serialization/write_json.h"

#include <cppspec.hpp>

using deluge::serialization::parseJson;
using deluge::serialization::Value;

namespace {
bool survives(std::string_view text) {
	auto parsed = parseJson(text);
	auto again = parseJson(deluge::serialization::write_json(*parsed));
	return parsed && again && *again == *parsed;
}
} // namespace

// clang-format off
describe write_json("writing a document tree as JSON", ${
	it("writes a tree that parses back to itself", _ {
		expect(survives(R"({"song": {"a": 1, "b": "96", "c": true, "d": null}})")).to_equal(true);
		expect(survives(R"({"sections": [{"section": {"id": 0}}, {"section": {"id": 1}}]})")).to_equal(true);
		expect(survives(R"({"x": 1, "x": 2})")).to_equal(true);          // duplicate keys kept, in order
		expect(survives(R"({"m": {}, "a": []})")).to_equal(true);
	});

	it("escapes text JSON cannot hold raw", _ {
		auto map = Value::makeMap();
		map.set("name", Value::makeScalar("a \"quoted\" back\\slash\tand tab"));
		auto back = parseJson(deluge::serialization::write_json(map));
		expect(back->find("name")->str()).to_equal(std::string("a \"quoted\" back\\slash\tand tab"));
	});

	it("writes one entry per line, tab-indented", _ {
		auto map = Value::makeMap();
		map.set("a", Value::makeNumber(1));
		map.set("b", Value::makeScalar("x"));
		expect(deluge::serialization::write_json(map)).to_equal(std::string("{\n\t\"a\": 1,\n\t\"b\": \"x\"\n}\n"));
	});
});

CPPSPEC_SPEC(write_json)
