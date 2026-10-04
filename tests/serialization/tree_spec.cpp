#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"

#include <cppspec.hpp>

using namespace deluge::serialization;

// clang-format off
describe tree("reading a document tree", ${
	it("finds repeated children in XML's shape and JSON's alike", _ {
		auto xml = parseXml(R"(<song><sections><section id="0"/><section id="1"/></sections></song>)");
		auto json = parseJson(R"({"song": {"sections": [{"section": {"id": 0}}, {"section": {"id": 1}}]}})");
		const Value& xs = *xml->find("song")->find("sections");
		const Value& js = *json->find("song")->find("sections");
		expect(elements_named(xs, "section").size()).to_equal(2u);
		expect(elements_named(js, "section").size()).to_equal(2u);
		expect(*int_of(elements_named(xs, "section")[1]->find("id"))).to_equal(1);
		expect(*int_of(elements_named(js, "section")[1]->find("id"))).to_equal(1);
	});

	it("lists differently named children in order, in either shape", _ {
		auto xml = parseXml(R"(<song><instruments><sound id="1"/><kit id="2"/><sound id="3"/></instruments></song>)");
		auto json = parseJson(R"({"song": {"instruments": [{"sound": {"id": 1}}, {"kit": {"id": 2}}, {"sound": {"id": 3}}]}})");
		for (const auto* doc : {&*xml, &*json}) {
			auto list = children(*doc->find("song")->find("instruments"));
			expect(list.size()).to_equal(3u);
			expect(std::string(list[1].name)).to_equal(std::string("kit"));
			expect(*int_of(list[2].node->find("id"))).to_equal(3);
		}
	});

	it("finds nothing in a scalar or a missing container", _ {
		auto scalar = Value::makeScalar("x");
		expect(elements_named(scalar, "section").empty()).to_equal(true);
	});

	it("reads integers and hex as the tree reader does", _ {
		auto doc = parseJson(R"({"n": -7, "t": "12", "bad": "x"})");
		expect(*int_of(doc->find("n"))).to_equal(-7);
		expect(*int_of(doc->find("t"))).to_equal(12);
		expect(*int_of(doc->find("bad"))).to_equal(0);
		expect(int_of(nullptr).has_value()).to_equal(false);
		auto hex = Value::makeScalar("0x00FF");
		expect(*hex_of(&hex)).to_equal(255u);
		auto notHex = Value::makeScalar("FF");
		expect(hex_of(&notHex).has_value()).to_equal(false);
		expect(*text_of(doc->find("t"))).to_equal(std::string("12"));
		expect(text_of(nullptr) == nullptr).to_equal(true);
	});
});

CPPSPEC_SPEC(tree)
