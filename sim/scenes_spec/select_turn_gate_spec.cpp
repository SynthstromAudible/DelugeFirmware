/// @file
/// @brief The guest's select-encoder gate under the ported menu: `SoundEditor::selectEncoderAction` edits only in the
/// UI
///        modes `selectEncoderUIModes` lists, and not while LEARN holds learn mode.
///
/// The menu is opened on the global guest over the instrument clip view, as `openShellMenu` leaves it, and every turn
/// arrives through `ShellHostUI::selectEncoderAction`, the hook the firmware calls. The page is the MIDI follow
/// CHANNEL list: a turn moves its cursor, and its first row, CHANNEL A, allows learn.

#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include "support/global_guest_menu.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace {

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace spec = deluge::scenes::spec;
using deluge::scenes::menu_scene;

/// @brief Put `currentUIMode` back however an example ends.
struct UiModeGuard {
	UiModeGuard() = default;
	UiModeGuard(const UiModeGuard&) = delete;
	UiModeGuard& operator=(const UiModeGuard&) = delete;
	~UiModeGuard() { currentUIMode = UI_MODE_NONE; }
};

/// @brief Open the follow CHANNEL list on the global guest over the instrument clip view, and let it draw.
bool open_follow_list(spec::SongFixture& fx) {
	setRootUILowLevel(&instrumentClipView);
	(void)menu::assign_node_ids(nodes::kAllRoots);
	menu_scene().open(nodes::midi_follow_channel, spec::menu_context_at(fx, menu::MenuContext::Root::Settings));
	shellHostUI.setInitialScene(
	    deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(nodes::midi_follow_channel)});
	if (!openUI(&shellHostUI)) {
		return false;
	}
	for (int32_t i = 0; i < 4; i++) {
		(void)shellHostUI.timerCallback();
	}
	return true;
}

int32_t cursor() {
	return menu_scene().state().list[menu_scene().state().depth].cursor;
}

/// @brief One select detent through the guest, and the tick that acts on it.
void select_turn() {
	shellHostUI.selectEncoderAction(1);
	(void)shellHostUI.timerCallback();
}

} // namespace

// clang-format off
describe select_turn_gate("gui::ui::ShellHostUI::selectEncoderAction under the ported menu", $ {
	it("moves the menu with no UI mode on", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		const int32_t before = cursor();
		select_turn();
		expect(cursor()).to_equal(before + 1);
	});

	it("moves the menu while auditioning, which selectEncoderUIModes lists", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		const int32_t before = cursor();
		select_turn();
		expect(cursor()).to_equal(before + 1);
	});

	it("leaves the menu alone with the horizontal encoder's button held, which selectEncoderUIModes does not list", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		const int32_t before = cursor();
		select_turn();
		expect(cursor()).to_equal(before);
	});

	it("leaves the menu alone while LEARN is held, and moves it once LEARN is released", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		const int32_t before = cursor();
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, true);
		(void)shellHostUI.timerCallback();
		expect(menu_scene().learn_mode()).to_be_true();
		select_turn();
		expect(cursor()).to_equal(before);
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, false);
		(void)shellHostUI.timerCallback();
		expect(menu_scene().learn_mode()).to_be_false();
		select_turn();
		expect(cursor()).to_equal(before + 1);
	});
});
// clang-format on

DELUGE_SPEC(select_turn_gate_spec, select_turn_gate)
