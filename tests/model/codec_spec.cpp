#include "deluge-project/codec.h"
#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"

#include <cppspec.hpp>

using namespace deluge::project;
using deluge::serialization::parseJson;
using deluge::serialization::parseXml;

// clang-format off
describe codec("the model's codec foundation", ${
	it("converts sync levels to and from the file losslessly", _ {
		bool all = true;
		for (int32_t magnitude = 0; magnitude <= 4; magnitude++) {
			for (int32_t level = 0; level <= 9; level++) {
				if (level >= 1 && level - 1 + magnitude < 1) {
					continue; // the file clamps at 1: those levels cannot be stored at this magnitude
				}
				all = all && sync_level_from_file(sync_level_to_file(level, magnitude), magnitude) == level;
			}
		}
		expect(all).to_equal(true);
		expect(sync_level_from_file(0, 3)).to_equal(0);
		expect(sync_level_to_file(0, 3)).to_equal(0);
	});

	it("reads an old song version and its tick magnitude", _ {
		auto doc = parseXml(R"(<song firmwareVersion="3.0.0" inputTickMagnitude="3"></song>)");
		DecodeContext found = decode_context(*doc, *doc->find("song"));
		expect(found.input_tick_magnitude).to_equal(3);
		expect(found.swing_interval_is_relative).to_equal(false);
	});

	it("reads sync values as relative from 3.1.0-alpha2 and from every community version", _ {
		auto official = parseJson(R"({"song": {"firmwareVersion": "3.1.0-alpha2"}})");
		auto community = parseJson(R"({"song": {"firmwareVersion": "c1.2.0"}})");
		expect(decode_context(*official, *official->find("song")).swing_interval_is_relative).to_equal(true);
		expect(decode_context(*community, *community->find("song")).swing_interval_is_relative).to_equal(true);
	});

	it("takes the version of an old file from beside the song", _ {
		auto doc = parseXml(R"(<firmwareVersion>3.1.0</firmwareVersion><song></song>)");
		expect(decode_context(*doc, *doc->find("song")).swing_interval_is_relative).to_equal(true);
	});

	it("treats a song without a version as the oldest", _ {
		auto doc = parseXml(R"(<song></song>)");
		DecodeContext found = decode_context(*doc, *doc->find("song"));
		expect(found.swing_interval_is_relative).to_equal(false);
		expect(found.input_tick_magnitude).to_equal(1);
	});
});

CPPSPEC_SPEC(codec)
