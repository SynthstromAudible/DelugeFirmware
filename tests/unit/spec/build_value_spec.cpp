#include "storage/build_value.h"
#include "storage/value.h"
#include "storage/value_tree_deserializer.h"
#include <cppspec.hpp>
#include <string>

// clang-format off
describe build_value("deluge::storage::buildValue", ${
	using deluge::storage::Value;
	using deluge::storage::ValueTreeDeserializer;
	using deluge::storage::buildValue;
	it("round-trips a tree through the adapter", _{
		Value clip = Value::makeMap();
		clip.set("id", Value::makeScalar("7"));
		Value nr = Value::makeMap();
		nr.set("y", Value::makeScalar("5"));
		clip.set("noteRow", std::move(nr));

		ValueTreeDeserializer d{clip};
		d.match('{');                                  // position at the map, as the loader will
		Value rebuilt = buildValue(d);
		expect(rebuilt.find("id")->str()).to_equal(std::string("7"));
		expect(rebuilt.find("noteRow")->isMap()).to_be_true();
		expect(rebuilt.find("noteRow")->find("y")->str()).to_equal(std::string("5"));
		expect(rebuilt.entries()[0].first).to_equal(std::string("id"));       // order preserved
		expect(rebuilt.entries()[1].first).to_equal(std::string("noteRow"));
	});

	it("round-trips an array of multi-key element objects, in order", _{
		Value song = Value::makeMap();
		song.set("name", Value::makeScalar("Track"));

		Value clips = Value::makeArray();
		Value clip0 = Value::makeMap();
		clip0.set("id", Value::makeScalar("0"));
		clip0.set("length", Value::makeScalar("96"));
		clips.append(std::move(clip0));
		Value clip1 = Value::makeMap();
		clip1.set("id", Value::makeScalar("1"));
		clip1.set("length", Value::makeScalar("192"));
		clips.append(std::move(clip1));
		song.set("clips", std::move(clips));

		ValueTreeDeserializer d{song};
		d.match('{'); // position at the map, exactly as readFromFile would
		Value rebuilt = buildValue(d);

		expect(rebuilt.find("name")->str()).to_equal(std::string("Track"));
		const Value* rebuiltClips = rebuilt.find("clips");
		expect(rebuiltClips->isArray()).to_be_true();
		expect(rebuiltClips->items().size()).to_equal(static_cast<std::size_t>(2));

		const Value& e0 = rebuiltClips->items()[0];
		expect(e0.isMap()).to_be_true();
		expect(e0.find("id")->str()).to_equal(std::string("0"));
		expect(e0.find("length")->str()).to_equal(std::string("96"));

		const Value& e1 = rebuiltClips->items()[1];
		expect(e1.isMap()).to_be_true();
		expect(e1.find("id")->str()).to_equal(std::string("1"));
		expect(e1.find("length")->str()).to_equal(std::string("192"));

		// Whole-tree structural check: keys in order at the top level too.
		expect(rebuilt.entries()[0].first).to_equal(std::string("name"));
		expect(rebuilt.entries()[1].first).to_equal(std::string("clips"));
	});
});

CPPSPEC_SPEC(build_value);
