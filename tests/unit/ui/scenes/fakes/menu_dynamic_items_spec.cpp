/// @file
/// @brief The MIDI devices list (`menu/nodes/dynamic.cpp`) over the MIDI devices fake: it opens on the device the menu
///        points at when the port still lists it, and on DIN otherwise.

#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_state.h"
#include "ui/scenes/menu/nodes/dynamic.h"

#include "midi/device_id.h"

#include "fakes.h"

#include "cppspec.hpp"

#include <cstdint>

#include "support/spec_main.h"

using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::MenuState;

namespace nodes = deluge::ui::scenes::menu::nodes;

// clang-format off
describe menu_dynamic_items("scenes::menu::nodes::MidiDevicesItem -- the devices list", ${
	it("opens on the device the menu points at, and chooses it", _ {
		FakeHost host;
		host.midi.list = {deluge::midi::kDin, deluge::midi::kUpstreamUsb1, deluge::midi::kFirstHosted};
		MenuContext ctx{};
		ctx.ports = host.ports();
		ctx.midi_device = deluge::midi::kFirstHosted;
		MenuState state{};
		nodes::kMidiDevicesItem.on_focus(ctx, state);
		expect(int32_t{state.list[0].cursor}).to_equal(2);
		expect(state.walk.midi_device_choice.value).to_equal(deluge::midi::kFirstHosted.value);
	});

	it("opens on the first device when the port no longer lists the one pointed at, and on DIN with none listed", _ {
		FakeHost host;
		host.midi.list = {deluge::midi::kUpstreamUsb1, deluge::midi::kDin};
		MenuContext ctx{};
		ctx.ports = host.ports();
		ctx.midi_device = deluge::midi::kFirstHosted;
		MenuState state{};
		nodes::kMidiDevicesItem.on_focus(ctx, state);
		expect(int32_t{state.list[0].cursor}).to_equal(0);
		expect(state.walk.midi_device_choice.value).to_equal(deluge::midi::kUpstreamUsb1.value);

		host.midi.list.clear();
		nodes::kMidiDevicesItem.on_focus(ctx, state);
		expect(state.walk.midi_device_choice.value).to_equal(deluge::midi::kDin.value);
	});
});
// clang-format on

DELUGE_SPEC(menu_dynamic_items_spec, menu_dynamic_items)
