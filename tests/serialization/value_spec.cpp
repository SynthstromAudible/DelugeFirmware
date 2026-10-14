#include "serialization/value.h"
#include <cppspec.hpp>

// clang-format off
describe value("deluge::serialization::Value", ${
	using deluge::serialization::Value;
	it("scalar holds its string", _{
		Value v = Value::makeScalar("42");
		expect(v.isScalar()).to_be_true();
		expect(v.str()).to_equal(std::string("42"));
	});
	it("map preserves insertion order and finds by key", _{
		Value m = Value::makeMap();
		m.set("id", Value::makeScalar("7"));
		m.set("section", Value::makeScalar("2"));
		expect(m.isMap()).to_be_true();
		expect(m.entries().size()).to_equal(std::size_t{2});
		expect(m.entries()[0].first).to_equal(std::string("id"));       // order, not sorted
		expect(m.entries()[1].first).to_equal(std::string("section"));
		expect(m.find("id")->str()).to_equal(std::string("7"));
		expect(m.find("missing") == nullptr).to_be_true();
	});
	it("array appends in order", _{
		Value a = Value::makeArray();
		a.append(Value::makeScalar("a"));
		a.append(Value::makeScalar("b"));
		expect(a.isArray()).to_be_true();
		expect(a.items().size()).to_equal(std::size_t{2});
		expect(a.items()[1].str()).to_equal(std::string("b"));
	});
	it("compares structure, order, text and kind", _{
		using deluge::serialization::ScalarKind;
		Value a = Value::makeMap();
		a.set("x", Value::makeNumber(96));
		a.set("y", Value::makeScalar("96"));
		Value b = Value::makeMap();
		b.set("x", Value::makeNumber(96));
		b.set("y", Value::makeScalar("96"));
		expect(a == b).to_be_true();
		expect(a.find("x")->scalarKind() == ScalarKind::NUMBER).to_be_true();
		expect(*a.find("x") == *a.find("y")).to_be_false();   // number 96 is not text "96"
		Value c = Value::makeMap();
		c.set("y", Value::makeScalar("96"));
		c.set("x", Value::makeNumber(96));
		expect(a == c).to_be_false();                            // order matters
	});
});
// clang-format on

CPPSPEC_SPEC(value);
