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
	it("maps both self-closing and empty open/close elements to an empty Map", _{
		// A truly-empty element must parse identically whether written <n/> or <n></n> -- both are
		// empty Maps, never Scalar(""). Deluge writes empty array containers as <tag></tag>, so this
		// keeps XML-mode replay of an empty container equivalent to a self-closing one.
		auto selfClosing = parseXml(R"(<n/>)");
		auto openClose = parseXml(R"(<n></n>)");
		expect(selfClosing.has_value()).to_be_true();
		expect(openClose.has_value()).to_be_true();
		expect(selfClosing->find("n")->isMap()).to_be_true();
		expect(openClose->find("n")->isMap()).to_be_true();
		expect(selfClosing->find("n")->entries().size()).to_equal(std::size_t{0});
		expect(openClose->find("n")->entries().size()).to_equal(std::size_t{0});
		// Whitespace-only body is still empty; a non-empty text body stays a Scalar.
		expect(parseXml(R"(<n>  </n>)")->find("n")->isMap()).to_be_true();
		expect(parseXml(R"(<n>0</n>)")->find("n")->isScalar()).to_be_true();
	});
	it("unescapes entities and returns nullopt on malformed input", _{
		expect(parseXml(R"(<n>a&amp;b</n>)")->find("n")->str()).to_equal(std::string("a&b"));
		expect(parseXml("<n>unterminated").has_value()).to_be_false();
	});
	it("drops stray text alongside child elements (malformed but tolerated, matches XMLDeserializer)", _{
		// issue #2514: a stray '>' turns <kitParams>'s attributes into text before its real children.
		// XMLDeserializer skips inter-tag text once an element has structure; parseXml must drop it, keep
		// the children, and not fail the whole parse.
		auto v = parseXml(R"(<p>a="1" b="2"<c v="3"/></p>)");
		expect(v.has_value()).to_be_true();
		expect(v->find("p")->isMap()).to_be_true();
		expect(v->find("p")->find("c")->find("v")->str()).to_equal(std::string("3"));
		expect(v->find("p")->find("a")).to_equal(nullptr); // the stray "attribute" text is gone
	});
	it("accepts digit-started names (Deluge numeric CC labels)", _{
		// XMLDeserializer accepts any non-delimiter char in a name; Deluge writes <ccLabels 0="" 2=""/>.
		auto v = parseXml(R"(<ccLabels 0="a" 2="b" 127="c"/>)");
		expect(v.has_value()).to_be_true();
		auto* cc = v->find("ccLabels");
		expect(cc->find("0")->str()).to_equal(std::string("a"));
		expect(cc->find("2")->str()).to_equal(std::string("b"));
		expect(cc->find("127")->str()).to_equal(std::string("c"));
	});
});

CPPSPEC_SPEC(parse_xml);
