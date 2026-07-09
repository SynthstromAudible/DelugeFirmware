#include "storage/parse_xml.h"
#include "storage/value.h"
#include <cppspec.hpp>

// clang-format off
describe parse_xml("deluge::storage::parseXml", ${
	using deluge::storage::parseXml;
	it("maps attributes to ordered scalar entries", _{
		auto v = parseXml(R"(<clip section="0" length="384"/>)");
		expect(v.has_value()).to_be_true();
		expect(v->isMap()).to_be_true();
		expect(v->find("clip")->isMap()).to_be_true();          // top level is the element
		const auto* clip = v->find("clip");
		expect(clip->entries()[0].first).to_equal(std::string("section"));  // attribute order
		expect(clip->find("length")->str()).to_equal(std::string("384"));
	});
	it("maps a text-only element to a scalar", _{
		auto v = parseXml(R"(<param><cc>0</cc><value>0x0000</value></param>)");
		const auto* p = v->find("param");
		expect(p->find("cc")->str()).to_equal(std::string("0"));
		expect(p->find("value")->str()).to_equal(std::string("0x0000")); // raw hex text
	});
	it("keeps repeated child names as repeated entries", _{
		auto v = parseXml(R"(<clips><clip a="1"/><clip a="2"/></clips>)");
		const auto* clips = v->find("clips");
		expect(clips->entries().size()).to_equal(std::size_t{2});
		expect(clips->entries()[0].first).to_equal(std::string("clip"));
		expect(clips->entries()[1].first).to_equal(std::string("clip"));
		expect(clips->entries()[1].second.find("a")->str()).to_equal(std::string("2"));
	});
	it("unescapes entities and returns nullopt on malformed input", _{
		expect(parseXml(R"(<n>a&amp;b</n>)")->find("n")->str()).to_equal(std::string("a&b"));
		expect(parseXml("<n>unterminated").has_value()).to_be_false();
	});
});

CPPSPEC_SPEC(parse_xml);
