#include "deluge-project/song.h"

#include <cppspec.hpp>

using namespace deluge::project;

// clang-format off
describe slot_address("a sound's slot named by its owner", ${
	it("resolves a synth track's slot by its output id and a kit row's by its drum id", _ {
		Song song;
		song.output_structure.getOrCreate(4).instrument.emplace().engine = "deluge.instrument";
		song.drum_structure.getOrCreate(9).instrument.emplace().engine = "deluge.instrument";
		expect(song.resolveSlot({SlotAddress::Kind::OUTPUT, 4})).to_equal(&*song.output_structure.find(4)->instrument);
		expect(song.resolveSlot({SlotAddress::Kind::DRUM, 9})).to_equal(&*song.drum_structure.find(9)->instrument);
	});

	it("resolves nothing for an owner without a slot or an unknown id", _ {
		Song song;
		song.output_structure.getOrCreate(5); // a kit or a MIDI track: no instrument slot
		expect(song.resolveSlot({SlotAddress::Kind::OUTPUT, 5})).to_be_null();
		expect(song.resolveSlot({SlotAddress::Kind::OUTPUT, 6})).to_be_null();
		expect(song.resolveSlot({SlotAddress::Kind::DRUM, 6})).to_be_null();
	});
});

CPPSPEC_SPEC(slot_address)
