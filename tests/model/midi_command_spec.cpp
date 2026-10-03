#include "deluge-project/midi_command.h"

#include <cppspec.hpp>

using deluge::project::MidiCommand;
using deluge::project::MidiDeviceRef;

// clang-format off
describe midi_command("a section's MIDI launch command", ${
	it("is empty by default", _ {
		MidiCommand command;
		expect(command.contains_something()).to_equal(false);
		expect(command.device.has_value()).to_equal(false);
	});

	it("names a channel or zone, a note or CC, and optionally a device", _ {
		MidiCommand command{.channel_or_zone = 2, .note_or_cc = 60,
		                    .device = MidiDeviceRef{.port = MidiDeviceRef::Port::HOSTED, .name = "Keys",
		                                            .vendor_id = 0x1234, .product_id = 0x0042}};
		expect(command.contains_something()).to_equal(true);
		MidiCommand same = command;
		expect(same == command).to_equal(true);
		same.device->product_id = 0x0043;
		expect(same == command).to_equal(false);
	});
});

CPPSPEC_SPEC(midi_command)
