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
#include "model/project_bridge/param_ids.h"
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

/// @brief The MIDI follow CHANNEL list (Settings) on the guest over the clip view: a vertical list at depth 0 whose
///        first row, CHANNEL A, opens a leaf that allows learn (`select_turn_gate_spec.cpp`'s page).
bool open_follow_list(spec::SongFixture& fx) {
	setRootUILowLevel(&instrumentClipView);
	(void)menu::assign_node_ids(nodes::kAllRoots);
	menu_scene().open(nodes::midi_follow_channel, spec::menu_context_at(fx, menu::MenuContext::Root::Settings));
	shellHostUI.setInitialScene(
	    deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(nodes::midi_follow_channel)});
	if (!openUI(&shellHostUI)) {
		return false;
	}
	(void)shellHostUI.timerCallback();
	return true;
}

/// @brief A press and release of @p b through the guest, and the ticks that act on them.
void tap(deluge::hid::Button b) {
	(void)shellHostUI.buttonAction(b, true);
	(void)shellHostUI.buttonAction(b, false);
	(void)shellHostUI.timerCallback();
	(void)shellHostUI.timerCallback();
}

uint8_t depth() {
	return menu_scene().state().depth;
}

bool menu_on_guest() {
	for (const deluge::ui::shell::Scene* scene : shellHostUI.shell().stack().scenes()) {
		if (scene == &menu_scene()) {
			return true;
		}
	}
	return false;
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

describe menu_mode_gates_buttons("gui::ui::ShellHostUI::buttonAction -- SoundEditor::buttonAction's UI-mode gates", $ {
	it("LEARN with a note auditioning enters no learn mode", _ {
		spec::ShellMenuOn on;
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_lpf_page_over(&instrumentClipView, fx.clip)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, true);
		(void)shellHostUI.timerCallback();
		expect(menu_scene().learn_mode()).to_be_false();
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, false);
		(void)shellHostUI.timerCallback();
	});

	it("LEARN with no UI mode enters learn mode", _ {
		spec::ShellMenuOn on;
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_lpf_page_over(&instrumentClipView, fx.clip)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, true);
		(void)shellHostUI.timerCallback();
		expect(menu_scene().learn_mode()).to_be_true();
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, false);
		(void)shellHostUI.timerCallback();
	});

	it("SHIFT + LEARN with a note auditioning forgets nothing", _ {
		spec::ShellMenuOn on;
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_lpf_page_over(&instrumentClipView, fx.clip)).to_be_true();
		const std::optional<deluge::project::OwnerParamTarget> target = menu_scene().learn_target();
		expect(target.has_value()).to_be_true();
		if (!target) {
			return;
		}
		const menu::MenuContext& ctx = menu_scene().menu_context();
		expect(ctx.sink->learn(*target, deluge::project::MidiCommand{.channel_or_zone = 18, .note_or_cc = 20},
		                       std::nullopt))
		    .to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, true);
		(void)shellHostUI.timerCallback();
		expect(ctx.model->mappings.find(*target) != nullptr).to_be_true();
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, false);
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		(void)shellHostUI.timerCallback();
	});

	it("SHIFT + LEARN with a shortcut pad held still unlearns: a pad fed to the menu sets no UI mode on main", _ {
		spec::ShellMenuOn on;
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_lpf_page_over(&instrumentClipView, fx.clip)).to_be_true();
		const std::optional<deluge::project::OwnerParamTarget> target = menu_scene().learn_target();
		expect(target.has_value()).to_be_true();
		if (!target) {
			return;
		}
		const menu::MenuContext& ctx = menu_scene().menu_context();
		expect(ctx.sink->learn(*target, deluge::project::MidiCommand{.channel_or_zone = 18, .note_or_cc = 20},
		                       std::nullopt))
		    .to_be_true();
		currentUIMode = UI_MODE_NONE;
		spec::ShiftHeld shift_held; // legacy's side of the Shift the shortcut pad's gate reads
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
		(void)shellHostUI.padAction(8, 7, 127);
		(void)shellHostUI.timerCallback();
		// Precondition: the pad reached the shell, so the stand-in would see it held.
		expect(std::ranges::any_of(shellHostUI.shell().lowering().held_pads().cols,
		                           [](uint32_t col) { return col != 0; }))
		    .to_be_true();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, true);
		(void)shellHostUI.timerCallback();
		expect(menu_scene().menu_context().model->mappings.find(*target) == nullptr).to_be_true();
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, false);
		(void)shellHostUI.padAction(8, 7, 0);
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		(void)shellHostUI.timerCallback();
	});

	it("SELECT with the horizontal encoder's button held does not enter the row", _ {
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		tap(deluge::hid::button::SELECT_ENC);
		expect(depth()).to_equal(uint8_t{0});
		currentUIMode = UI_MODE_NONE;
		tap(deluge::hid::button::SELECT_ENC);
		expect(depth()).to_equal(uint8_t{1});
	});

	it("BACK below the root with the horizontal encoder's button held stays", _ {
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		tap(deluge::hid::button::SELECT_ENC);
		expect(depth()).to_equal(uint8_t{1});
		currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		tap(deluge::hid::button::BACK);
		expect(depth()).to_equal(uint8_t{1});
		currentUIMode = UI_MODE_NONE;
		tap(deluge::hid::button::BACK);
		expect(depth()).to_equal(uint8_t{0});
	});

	it("BACK at the root with the horizontal encoder's button held leaves the menu open", _ {
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		tap(deluge::hid::button::BACK);
		expect(menu_on_guest()).to_be_true();
		currentUIMode = UI_MODE_NONE;
		tap(deluge::hid::button::BACK);
		expect(menu_on_guest()).to_be_false();
	});

	it("two listed modes at once are not one of them: BACK below the root stays", _ {
		// Legacy compares `currentUIMode ==` each of the five; auditioning with notes pressed is neither.
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		tap(deluge::hid::button::SELECT_ENC);
		currentUIMode = UI_MODE_AUDITIONING | UI_MODE_NOTES_PRESSED;
		tap(deluge::hid::button::BACK);
		expect(depth()).to_equal(uint8_t{1});
	});

	it("BACK below the root in learn mode stays: legacy's UI_MODE_MIDI_LEARN is not among the five", _ {
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		tap(deluge::hid::button::SELECT_ENC); // CHANNEL A's own screen, which allows learn
		expect(depth()).to_equal(uint8_t{1});
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, true);
		(void)shellHostUI.timerCallback();
		expect(menu_scene().learn_mode()).to_be_true();
		tap(deluge::hid::button::BACK);
		expect(depth()).to_equal(uint8_t{1});
		(void)shellHostUI.buttonAction(deluge::hid::button::LEARN, false);
		(void)shellHostUI.timerCallback();
	});

	it("a select turn still moves the list in a mode that refuses SELECT's press", _ {
		// The turn's gate is `selectEncoderUIModes`, a range test: auditioning with notes pressed passes it.
		spec::GlobalGuestGuard guest_guard;
		spec::UiStackGuard ui_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_follow_list(fx)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING | UI_MODE_NOTES_PRESSED;
		tap(deluge::hid::button::BACK); // refused; sets the menu's gates for this mode
		const int32_t before = menu_scene().state().list[depth()].cursor;
		shellHostUI.selectEncoderAction(1);
		(void)shellHostUI.timerCallback();
		expect(menu_scene().state().list[depth()].cursor).to_equal(before + 1);
	});
});
// clang-format on

DELUGE_SPEC(menu_mode_gates_spec, menu_mode_gates_horizontal, menu_mode_gates_buttons)
