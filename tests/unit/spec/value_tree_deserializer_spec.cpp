#include "storage/value.h"
#include "storage/value_tree_deserializer.h"
#include <cppspec.hpp>
#include <string>
#include <vector>

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
	// Mirrors Song::readClipsFromFile: an array of single-key object-boxes, each box wrapping a
	// value object. Walks with `match('[')` then
	// `while (match('{') && *readNextTagOrAttributeName()) { … exitTag(nullptr,true); match('}'); }`
	// and a final `match(']')`. This FAILS if match('}')/match(']') no-op (only the first element
	// would be visited).
	it("iterates every box in an array like readClipsFromFile", _{
		Value arr = Value::makeArray();
		for (const char* v : {"10", "20"}) {
			Value box = Value::makeMap();
			Value clip = Value::makeMap();
			clip.set("x", Value::makeScalar(v));
			box.set("clip", std::move(clip));
			arr.append(std::move(box));
		}
		ValueTreeDeserializer d{arr};
		std::vector<int32_t> visited;
		const char* tagName;
		expect(d.match('[')).to_be_true();
		while (d.match('{') && *(tagName = d.readNextTagOrAttributeName())) {
			expect(std::string(tagName)).to_equal(std::string("clip"));
			d.match('{'); // descend into the value object, as the clip's own readFromFile would
			expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("x"));
			visited.push_back(d.readTagOrAttributeValueInt());
			d.exitTag(nullptr, true); // leave the value object
			d.match('}');             // leave the box
		}
		// The loop terminated because match('{') returned false on the exhausted array, and a final
		// match(']') closes the array.
		expect(d.match(']')).to_be_true();
		expect(visited.size()).to_equal(static_cast<std::size_t>(2));
		expect(visited[0]).to_equal(10);
		expect(visited[1]).to_equal(20);
	});
});

CPPSPEC_SPEC(value_tree_deserializer);
