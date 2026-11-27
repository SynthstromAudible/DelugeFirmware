/// @file
/// @brief The note editors' bridge (`menu/note_bridge.cpp`): the installed bridge is the one read back, and
///        installing none clears it.

#include "ui/scenes/menu/note_bridge.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using deluge::ui::scenes::menu::note_bridge;
using deluge::ui::scenes::menu::NoteBridge;
using deluge::ui::scenes::menu::set_note_bridge;

// clang-format off
describe menu_note_bridge("scenes::menu -- the note editors' bridge", ${
	it("reads back the bridge installed last, and nothing once it is cleared", _ {
		const NoteBridge first{};
		const NoteBridge second{};
		set_note_bridge(&first);
		expect(note_bridge()).to_equal(&first);
		set_note_bridge(&second);
		expect(note_bridge()).to_equal(&second);
		set_note_bridge(nullptr);
		expect(note_bridge()).to_be_null();
	});
});
// clang-format on

DELUGE_SPEC(menu_note_bridge_spec, menu_note_bridge)
