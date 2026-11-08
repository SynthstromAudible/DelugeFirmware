/// @file
/// @brief `SoundEditor`'s UI-mode arms under the ported menu: the keyboard's horizontal turn while one of its notes is
///        held (`SoundEditor::horizontalEncoderAction`), and the `currentUIMode` gates on LEARN, SELECT and BACK
///        (`SoundEditor::buttonAction`).
///
/// The menu is opened on the global guest over a real root, through the entry the firmware uses, and every input
/// arrives through the `ShellHostUI` hook the firmware calls. `currentUIMode` is set as the forwarded pads would
/// leave it.

#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include "support/feature_setting.h"
#include "support/global_guest_menu.h"
#include "support/menu_over_root.h"
#include "support/song_fixture.h"
#include "support/ui_mode_guard.h"

#include <algorithm>
#include <optional>

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace {

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace spec = deluge::scenes::spec;
using deluge::scenes::menu_scene;

/// @brief The isomorphic keyboard on @p clip, scrolled to 60 with rows a fourth apart: a turn of 1 makes it 61.
void seat_isomorphic(InstrumentClip* clip) {
	clip->keyboardState.currentLayout = KeyboardLayoutType::KeyboardLayoutTypeIsomorphic;
	clip->keyboardState.isomorphic.rowInterval = 5;
	clip->keyboardState.isomorphic.scrollOffset = 60;
}

int32_t keyboard_scroll(InstrumentClip* clip) {
	return clip->keyboardState.isomorphic.scrollOffset;
}

/// @brief The LPF page (SHIFT + its frequency's pad) over @p root, on the guest, ticked once.
bool open_lpf_page_over(RootUI* root, InstrumentClip* clip) {
	if (!spec::open_shortcut_over(root, clip, 8, 7)) {
		return false;
	}
	(void)shellHostUI.timerCallback();
	return true;
}

} // namespace

// clang-format off
describe menu_mode_gates_horizontal("gui::ui::ShellHostUI::horizontalEncoderAction -- the keyboard's arm", $ {
	it("with a keyboard note held, a turn on a horizontal page scrolls the keyboard", _ {
		spec::ShellMenuOn on;
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		spec::SongFixture fx = spec::make_synth_song();
		seat_isomorphic(fx.clip);
		expect(open_lpf_page_over(&keyboardScreen, fx.clip)).to_be_true();
		expect(menu_scene().focused_value() == &nodes::lpf_freq).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.horizontalEncoderAction(1);
		expect(keyboard_scroll(fx.clip)).to_equal(61);
	});

	it("with no note held, the turn stays with the menu", _ {
		spec::ShellMenuOn on;
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		spec::SongFixture fx = spec::make_synth_song();
		seat_isomorphic(fx.clip);
		expect(open_lpf_page_over(&keyboardScreen, fx.clip)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		(void)shellHostUI.horizontalEncoderAction(1);
		expect(keyboard_scroll(fx.clip)).to_equal(60);
	});

	it("only the audition alone: with the horizontal encoder's button also held, the turn stays with the menu", _ {
		// `currentUIMode == UI_MODE_AUDITIONING`, an equality.
		spec::ShellMenuOn on;
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		spec::SongFixture fx = spec::make_synth_song();
		seat_isomorphic(fx.clip);
		expect(open_lpf_page_over(&keyboardScreen, fx.clip)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING | UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		(void)shellHostUI.horizontalEncoderAction(1);
		expect(keyboard_scroll(fx.clip)).to_equal(60);
	});

	it("over the clip view an audition leaves the keyboard alone", _ {
		spec::ShellMenuOn on;
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		spec::SongFixture fx = spec::make_synth_song();
		seat_isomorphic(fx.clip);
		expect(open_lpf_page_over(&instrumentClipView, fx.clip)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.horizontalEncoderAction(1);
		expect(keyboard_scroll(fx.clip)).to_equal(60);
	});
});
// clang-format on

DELUGE_SPEC(menu_mode_gates_spec, menu_mode_gates_horizontal)
