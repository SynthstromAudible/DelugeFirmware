#include "storage/parse_json.h"
#include "storage/value.h"
#include <cppspec.hpp>

// clang-format off
describe parse_json("deluge::storage::parseJson", ${
	using deluge::storage::parseJson;
	it("parses an object with ordered scalar members", _{
		auto v = parseJson(R"({"id":7,"section":2})");
		expect(v.has_value()).to_be_true();
		expect(v->isMap()).to_be_true();
		expect(v->entries()[0].first).to_equal(std::string("id"));      // insertion order
		expect(v->find("id")->str()).to_equal(std::string("7"));        // raw token text
	});
	it("parses nested arrays and objects", _{
		auto v = parseJson(R"({"clips":[{"id":1},{"id":2}]})");
		expect(v.has_value()).to_be_true();
		const auto* clips = v->find("clips");
		expect(clips->isArray()).to_be_true();
		expect(clips->items().size()).to_equal(std::size_t{2});
		expect(clips->items()[1].find("id")->str()).to_equal(std::string("2"));
	});
	it("stores strings unquoted", _{
		auto v = parseJson(R"({"name":"kick"})");
		expect(v->find("name")->str()).to_equal(std::string("kick"));
	});
	it("unescapes strings", _{
		auto v = parseJson(R"({"name":"kick\n\"snare\"\\hat"})");
		expect(v.has_value()).to_be_true();
		expect(v->find("name")->str()).to_equal(std::string("kick\n\"snare\"\\hat"));
	});
	it("returns nullopt on malformed input", _{
		expect(parseJson(R"({"id":)").has_value()).to_be_false();
	});
	it("returns nullopt on trailing garbage", _{
		expect(parseJson(R"({"id":7} garbage)").has_value()).to_be_false();
	});
	it("returns nullopt on missing comma", _{
		expect(parseJson(R"({"id":7 "section":2})").has_value()).to_be_false();
	});
	it("parses top-level scalars", _{
		auto v = parseJson("true");
		expect(v.has_value()).to_be_true();
		expect(v->isScalar()).to_be_true();
		expect(v->str()).to_equal(std::string("true"));
	});
	it("parses an empty object and empty array", _{
		auto obj = parseJson("{}");
		expect(obj.has_value()).to_be_true();
		expect(obj->isMap()).to_be_true();
		expect(obj->entries().size()).to_equal(std::size_t{0});

		auto arr = parseJson("[]");
		expect(arr.has_value()).to_be_true();
		expect(arr->isArray()).to_be_true();
		expect(arr->items().size()).to_equal(std::size_t{0});
	});
});

CPPSPEC_SPEC(parse_json);
