#include "deluge-project/song.h"

#include <cppspec.hpp>

using namespace deluge::project;

// clang-format off
describe controls_address("an owner's controller mappings named by the owner", ${
	it("resolves an output's, a kit row's and the song's controls", _ {
		Song song;
		song.output_structure.getOrCreate(4);
		song.drum_structure.getOrCreate(9);
		expect(song.resolveControls({ControlsAddress::Kind::OUTPUT, 4})).to_equal(&song.output_structure.find(4)->controls);
		expect(song.resolveControls({ControlsAddress::Kind::DRUM, 9})).to_equal(&song.drum_structure.find(9)->controls);
		expect(song.resolveControls({ControlsAddress::Kind::SONG, 0})).to_equal(&song.controls);
	});

	it("resolves nothing for an unknown output or drum", _ {
		Song song;
		expect(song.resolveControls({ControlsAddress::Kind::OUTPUT, 6})).to_be_null();
		expect(song.resolveControls({ControlsAddress::Kind::DRUM, 6})).to_be_null();
	});
});

CPPSPEC_SPEC(controls_address)
