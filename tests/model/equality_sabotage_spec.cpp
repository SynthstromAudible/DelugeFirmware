#include "equality/equality.h"
#include "serialization/parse_xml.h"
#include "serialization/write_json.h"

#include <cppspec.hpp>
#include <fstream>
#include <sstream>

using namespace deluge::project;

namespace {
std::string fixture() {
	std::ifstream in(DELUGE_MODEL_RECORDS);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}
} // namespace

// clang-format off
describe equality_sabotage("the model-equality gate", ${
	it("passes the song holding every record", _ {
		auto result = check_equality(fixture(), false);
		expect(result.has_value()).to_equal(true);
	});

	it("fails, naming the record, when encode drops a field", _ {
		EqualityFaults faults;
		faults.on_encoded = [](deluge::serialization::Value& document) {
			auto song = deluge::serialization::Value::makeMap();
			for (const auto& [key, value] : document.find("song")->entries()) {
				if (key != "swingAmount") {
					song.set(key, value);
				}
			}
			document = deluge::serialization::Value::makeMap();
			document.set("song", std::move(song));
		};
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_equal(false);
		expect(result.error().find("groove") != std::string::npos).to_equal(true);
	});

	it("fails, naming the record, when decode misreads a field", _ {
		EqualityFaults faults;
		faults.on_decoded = [](SongDocument& document) { document.groove.swing_amount += 1; };
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_equal(false);
		expect(result.error().find("groove") != std::string::npos).to_equal(true);
	});

	it("finds an XML song and its JSON save the same song", _ {
		const std::string json = deluge::serialization::write_json(
		    encode_song(*decode_song(*deluge::serialization::parseXml(fixture())), "c1.0.0"));
		auto result = compare_songs(fixture(), false, json, true);
		expect(result.has_value()).to_equal(true);
	});

	it("names the record when two saves are different songs", _ {
		auto changed = decode_song(*deluge::serialization::parseXml(fixture()));
		changed->groove.swing_amount += 1;
		const std::string json = deluge::serialization::write_json(encode_song(*changed, "c1.0.0"));
		auto result = compare_songs(fixture(), false, json, true);
		expect(!result && result.error().find("groove") != std::string::npos).to_equal(true);
	});

	it("finds two documents with the same tree the same, whatever their whitespace", _ {
		auto result = compare_trees(R"(<sound a="1"><osc1 type="sine"/></sound>)", false,
		                            "<sound\n  a=\"1\">\n<osc1\n type=\"sine\"></osc1></sound>", false);
		expect(result.has_value()).to_equal(true);
	});

	it("names the first path where two trees differ", _ {
		auto result = compare_trees(R"(<sound a="1"><osc1 type="sine"/></sound>)", false,
		                            R"(<sound a="1"><osc1 type="saw"/></sound>)", false);
		expect(!result && result.error().find("sound/osc1/type") != std::string::npos).to_equal(true);
	});
});

CPPSPEC_SPEC(equality_sabotage)
