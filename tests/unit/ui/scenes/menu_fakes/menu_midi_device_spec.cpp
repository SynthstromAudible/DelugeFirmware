/// @file
/// @brief A MIDI device's own settings (`menu/nodes/midi_device.cpp`) over the MIDI devices fake: the device the
///        context names is the one read and written, and no device is no write.

#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/nodes/midi_device.h"

#include "midi/device_id.h"

#include "fakes.h"

#include "cppspec.hpp"

#include <cstdint>

#include "support/spec_main.h"

using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::menu::MenuContext;

namespace nodes = deluge::ui::scenes::menu::nodes;

// clang-format off
describe menu_midi_device("scenes::menu::nodes -- a MIDI device's settings", ${
	it("writes the named device's clock-out to the MIDI devices port and reads it back from there", _ {
		FakeHost host;
		MenuContext ctx{};
		ctx.ports = host.ports();
		ctx.midi_device = deluge::midi::kDin;
		expect(nodes::get_device_send_clock(ctx)).to_equal(0);
		nodes::set_device_send_clock(ctx, 1);
		expect(host.midi.writes.size()).to_equal(size_t{1});
		if (host.midi.writes.size() == 1) {
			expect(host.midi.writes[0].first.value).to_equal(deluge::midi::kDin.value);
			expect(host.midi.writes[0].second.send_clock).to_be_true();
			// The rest of the device's settings are written as they were.
			expect(host.midi.writes[0].second.receive_clock).to_be_false();
		}
		expect(nodes::get_device_send_clock(ctx)).to_equal(1);
	});

	it("reads off and writes nothing when the context names no device", _ {
		FakeHost host;
		MenuContext ctx{};
		ctx.ports = host.ports();
		host.midi.kept[deluge::midi::kDin.value].send_clock = true;
		expect(nodes::get_device_send_clock(ctx)).to_equal(0);
		nodes::set_device_send_clock(ctx, 1);
		expect(host.midi.writes.size()).to_equal(size_t{0});
	});
});
// clang-format on

DELUGE_SPEC(menu_midi_device_spec, menu_midi_device)
