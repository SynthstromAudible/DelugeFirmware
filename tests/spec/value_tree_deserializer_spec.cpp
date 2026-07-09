#include "storage/value.h"
#include "storage/value_tree_deserializer.h"
#include <cppspec.hpp>

// clang-format off
describe value_tree_deserializer("ValueTreeDeserializer", ${
	using deluge::storage::Value;
	using deluge::storage::ValueTreeDeserializer;
	it("replays scalar members in order", _{
		Value clip = Value::makeMap();
		clip.set("id", Value::makeScalar("7"));
		clip.set("section", Value::makeScalar("2"));
		ValueTreeDeserializer d{clip};
		d.match('{');
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("id"));
		expect(d.readTagOrAttributeValueInt()).to_equal(7);
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("section"));
		expect(d.readTagOrAttributeValueInt()).to_equal(2);
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string(""));
	});
	it("descends into a nested object and pops back", _{
		Value root = Value::makeMap();
		Value inner = Value::makeMap();
		inner.set("y", Value::makeScalar("5"));
		root.set("noteRow", std::move(inner));
		ValueTreeDeserializer d{root};
		d.match('{');
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("noteRow"));
		expect(d.match('{')).to_be_true();
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("y"));
		expect(d.readTagOrAttributeValueInt()).to_equal(5);
		d.exitTag();
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string(""));
	});
	it("match('{') returns false (no consume) on a scalar", _{
		Value m = Value::makeMap();
		m.set("id", Value::makeScalar("7"));
		ValueTreeDeserializer d{m};
		d.match('{');
		d.readNextTagOrAttributeName();
		expect(d.match('{')).to_be_false();
		expect(d.readTagOrAttributeValueInt()).to_equal(7);
	});
});

CPPSPEC_SPEC(value_tree_deserializer);
