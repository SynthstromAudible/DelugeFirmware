#include "equality/equality.h"

#include <cppspec.hpp>
#include <fstream>
#include <sstream>

using namespace deluge::project;

namespace {
std::string fixture() {
	std::ifstream in(DELUGE_SYNTHETIC_DIR "/model_records.XML");
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}
} // namespace

// clang-format off
describe equality_sabotage("the model-equality gate", ${
	it("passes the synthetic song", _ {
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
});

CPPSPEC_SPEC(equality_sabotage)
