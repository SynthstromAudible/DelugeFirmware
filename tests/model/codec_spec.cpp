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
		expect(all).to_be_true();
		expect(sync_level_from_file(0, 3)).to_equal(0);
		expect(sync_level_to_file(0, 3)).to_equal(0);
	});

	it("reads a song's tick magnitude", _ {
		auto doc = parseXml(R"(<song firmwareVersion="c1.3.0" inputTickMagnitude="3"></song>)");
		expect(decode_context(*doc->find("song")).input_tick_magnitude).to_equal(3);
	});

	it("takes a tick magnitude of 1 when the song gives none", _ {
		auto doc = parseXml(R"(<song></song>)");
		expect(decode_context(*doc->find("song")).input_tick_magnitude).to_equal(1);
	});
});

CPPSPEC_SPEC(codec)
