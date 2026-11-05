/// @file
/// @brief The automation view under the ported menu: the facts about the root UI the menu and the knobs hold, read
///        again as the root, or the view's editor, changes while the menu is open; and the param the view follows as
///        the menu moves (`dispatchMenuTransitions`).
///
/// Runs over the real `automationView`, `instrumentClipView` and guest on a fresh synth (`spec::make_synth_song`). The
/// root facts open the menu through the shortcut entry (`support/menu_over_root.h`); the retarget examples drive the
/// real `MenuScene` in a `Shell` of their own, with the automation view seated as legacy's root, and drain the menu's
/// transition record after each tick as the guest's timer does.

#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_automation_glue.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h" // currentUIMode, setRootUILowLevel
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "modulation/params/param.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "synth/modulation/cable.h"

#include "support/global_guest_menu.h"
#include "support/menu_over_root.h"
#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace params = deluge::modulation::params;
namespace spec = deluge::scenes::spec;
namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
using deluge::scenes::menu_scene;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using spec::GlobalGuestGuard;
using spec::open_shortcut_over;
using spec::open_shortcut_over_automation;
using spec::ShellMenuOn;
using spec::UiStackGuard;

namespace {

/// The automation view on @p clip's overview, nothing selected.
void on_overview(InstrumentClip* clip) {
	automationView.onArrangerView = false;
	clip->lastSelectedParamKind = params::Kind::NONE;
	clip->lastSelectedParamID = params::kNoParamID;
	currentUIMode = UI_MODE_NONE;
}

/// The automation view's editor on @p clip's LPF frequency.
void on_lpf_freq(InstrumentClip* clip) {
	clip->lastSelectedParamKind = params::Kind::PATCHED;
	clip->lastSelectedParamID = params::LOCAL_LPF_FREQ;
}

/// Whether the automation view is on @p clip's param (@p kind, @p id), in its editor.
bool view_on(InstrumentClip* clip, params::Kind kind, int32_t id) {
	return automationView.inAutomationEditor() && clip->lastSelectedParamKind == kind
	       && clip->lastSelectedParamID == id;
}

/// The vibrato page's cable, LFO 1 onto pitch, in the view's numbering: its descriptor word.
constexpr int32_t kVibratoCable = static_cast<int32_t>(
    deluge::synth::CableTarget::depth_of(params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1).word);

constexpr uint8_t kKitX = 5, kKitY = 1;
constexpr uint8_t kMidiX = 5, kMidiY = 2;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr uint8_t kShiftX = 8, kShiftY = 0;
constexpr uint8_t kSelectEncoder = 3;

/// @brief The real `MenuScene` in a `Shell` of its own over fake surfaces, with the automation view seated as legacy's
///        root on a fresh synth's clip, on its overview; every tick is followed by the guest's transition drain.
struct Rig {
	UiStackGuard ui_guard;
	spec::SongFixture song = spec::make_synth_song();
	deluge::ui::shell::SceneRegistry reg = deluge::scenes::scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge::scenes::deluge_profile()};
	Shell shell{fake, deluge::scenes::deluge_profile(), reg, deluge::ui::shell::Services{},
	            deluge::ui::shell::ShellConfig{}};
	uint32_t now = 0;

	Rig() {
		on_overview(song.clip);
		setRootUILowLevel(&automationView);
		automationView.onMenuView = false;
	}
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// Open the menu at @p root, over the automation view.
	void open(const Node& root) {
		MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                                    /*kit_global_fx=*/false);
		ctx.root_is_automation = true;
		ctx.root_greyout = MenuContext::RootGreyout::SpareSidebar;
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		tick();
	}
	/// One tick of the shell, then the guest's drain of the menu's transition record.
	void tick() {
		now += 10;
		shell.tick(now);
		deluge::gui::ui::dispatchMenuTransitions();
	}
	void button(uint8_t x, uint8_t y, bool on) {
		now += 200; // past the page buttons' 140 ms debounce
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, now);
	}
	/// A press and release of the button at (@p x, @p y), each followed by a tick.
	void press(uint8_t x, uint8_t y) {
		button(x, y, true);
		tick();
		button(x, y, false);
		tick();
	}
	/// A shift-held shortcut pad at (@p x, @p y).
	void shortcut(uint8_t x, uint8_t y) {
		button(kShiftX, kShiftY, true);
		now += 10;
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, 64}, now);
		tick();
		now += 10;
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, 0}, now);
		button(kShiftX, kShiftY, false);
		tick();
	}
	/// @p detents clicks of the select encoder.
	void turn(int16_t detents) {
		now += 10;
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, now);
		tick();
	}
};

} // namespace

// clang-format off
describe automation_root_facts("the menu's root facts under the automation view", $ {
	it("are the automation view's overview at open", _ {
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		const MenuContext& ctx = menu_scene().menu_context();
		expect(ctx.root_is_automation).to_be_true();
		expect(ctx.root_is_keyboard).to_be_false();
		expect(ctx.root_greyout == MenuContext::RootGreyout::SpareSidebar).to_be_true();
	});

	it("follow the view into its editor, for the menu's greyout and the knobs", _ {
		// `SoundEditor::getGreyoutColsAndRows` re-reads `automationView.inAutomationEditor()` every time
		// (`sound_editor.cpp:497-500`): the editor greys nothing.
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		on_lpf_freq(fx.clip);
		shellHostUI.refreshRootFacts();
		expect(menu_scene().menu_context().root_greyout == MenuContext::RootGreyout::Nothing).to_be_true();
		expect(deluge::gui::ui::guestKnobsSeeAutomationEditor()).to_be_true();
		uint32_t cols = 0;
		uint32_t rows = 0;
		expect(shellHostUI.getGreyoutColsAndRows(&cols, &rows)).to_be_false(); // greys nothing

	});

	it("follow a new root", _ {
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		shellHostUI.refreshRootFacts();
		const MenuContext& ctx = menu_scene().menu_context();
		expect(ctx.root_is_automation).to_be_false();
		expect(ctx.root_greyout == MenuContext::RootGreyout::InstrumentClip).to_be_true();
		expect(deluge::gui::ui::guestKnobsSeeAutomationEditor()).to_be_false();
	});
});

describe automation_retarget("the automation view follows the menu's param", $ {
	it("selects nothing on SELECT into a page focused on a setting, and retargets as the column buttons move", _ {
		// `handlePotentialParamMenuChange`: entering the LPF page lands on MODE, which is no param; KIT and MIDI move
		// the column onto frequency and resonance, each a param, and the view follows.
		Rig r;
		r.open(nodes::sound_filters);
		r.press(kSelectEncButtonX, kSelectEncButtonY);
		expect(menu_scene().focused_value() == &nodes::lpf_mode).to_be_true();
		expect(automationView.inAutomationEditor()).to_be_false();
		r.press(kKitX, kKitY);
		expect(view_on(r.song.clip, params::Kind::PATCHED, params::LOCAL_LPF_FREQ)).to_be_true();
		r.press(kMidiX, kMidiY);
		expect(view_on(r.song.clip, params::Kind::PATCHED, params::LOCAL_LPF_RESONANCE)).to_be_true();
	});

	it("returns the view to its overview on BACK from a param to a list", _ {
		Rig r;
		r.open(nodes::sound_filters);
		r.press(kSelectEncButtonX, kSelectEncButtonY);
		r.press(kKitX, kKitY);
		expect(automationView.inAutomationEditor()).to_be_true();
		r.press(kBackX, kBackY);
		expect(menu_scene().focused() == &nodes::sound_filters).to_be_true();
		expect(automationView.inAutomationEditor()).to_be_false();
	});

	it("retargets on a shortcut, onto the param the menu was already on too", _ {
		Rig r;
		r.open(nodes::sound_filters);
		r.shortcut(8, 7);
		expect(view_on(r.song.clip, params::Kind::PATCHED, params::LOCAL_LPF_FREQ)).to_be_true();
		on_lpf_freq(r.song.clip);
		r.song.clip->lastSelectedParamID = params::LOCAL_HPF_FREQ; // the view moved on by itself
		r.shortcut(8, 7);
		expect(view_on(r.song.clip, params::Kind::PATCHED, params::LOCAL_LPF_FREQ)).to_be_true();
	});

	it("selects a cable from its page", _ {
		Rig r;
		r.open(nodes::sound_filters);
		r.shortcut(6, 2);
		expect(view_on(r.song.clip, params::Kind::PATCH_CABLE, kVibratoCable)).to_be_true();
		expect(r.song.clip->lastSelectedPatchSource == PatchSource::LFO_GLOBAL_1).to_be_true();
	});

	it("leaves the view on its param when BACK leaves the menu", _ {
		// `exitCompletely` sends nothing: the view stays where the menu left it (copied quirk).
		Rig r;
		r.open(nodes::sound_filters);
		r.shortcut(8, 7);
		expect(view_on(r.song.clip, params::Kind::PATCHED, params::LOCAL_LPF_FREQ)).to_be_true();
		r.press(kBackX, kBackY);
		expect(r.shell.stack().depth()).to_equal(size_t(0));
		expect(view_on(r.song.clip, params::Kind::PATCHED, params::LOCAL_LPF_FREQ)).to_be_true();
	});

	it("does not retarget on a select turn", _ {
		Rig r;
		r.open(nodes::sound_filters);
		r.shortcut(8, 7);
		r.song.clip->lastSelectedParamID = params::LOCAL_HPF_FREQ;
		r.turn(1);
		expect(view_on(r.song.clip, params::Kind::PATCHED, params::LOCAL_HPF_FREQ)).to_be_true();
	});

	it("does nothing over another root", _ {
		Rig r;
		setRootUILowLevel(&instrumentClipView);
		r.open(nodes::sound_filters);
		r.shortcut(8, 7);
		expect(r.song.clip->lastSelectedParamKind == params::Kind::NONE).to_be_true();
	});
});
describe automation_menu_view("CLIP, X_ENC and KEYBOARD reach the automation view from the menu", $ {
	it("enters the view on the menu's param in menu view on CLIP, and leaves it on a second CLIP", _ {
		// `Automation::buttonAction`'s CLIP arm, on a horizontal page's param column.
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		automationView.onMenuView = false;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		expect(shellHostUI.buttonAction(deluge::hid::button::CLIP_VIEW, true) == ActionResult::DEALT_WITH).to_be_true();
		(void)shellHostUI.buttonAction(deluge::hid::button::CLIP_VIEW, false);
		expect(getRootUI() == &automationView).to_be_true();
		expect(automationView.onMenuView).to_be_true();
		expect(automationView.previousUI == &instrumentClipView).to_be_true();
		expect(view_on(fx.clip, params::Kind::PATCHED, params::LOCAL_LPF_FREQ)).to_be_true();
		expect(menu_scene().menu_context().root_is_automation).to_be_true();
		(void)shellHostUI.buttonAction(deluge::hid::button::CLIP_VIEW, true);
		expect(getRootUI() == &instrumentClipView).to_be_true();
		expect(automationView.onMenuView).to_be_false();
		expect(menu_scene().menu_context().root_is_automation).to_be_false();
	});

	it("leaves CLIP on an item with no param to the menu", _ {
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		automationView.onMenuView = false;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 5)).to_be_true(); // LPF MODE, a setting
		expect(shellHostUI.buttonAction(deluge::hid::button::CLIP_VIEW, true) == ActionResult::NOT_DEALT_WITH)
		    .to_be_true();
		expect(getRootUI() == &instrumentClipView).to_be_true();
	});

	it("gives X_ENC on a param to the view under it, and not over another root", _ {
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		expect(shellHostUI.buttonAction(deluge::hid::button::X_ENC, true) == ActionResult::DEALT_WITH).to_be_true();
		expect(shellHostUI.buttonAction(deluge::hid::button::X_ENC, false) == ActionResult::DEALT_WITH).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		expect(shellHostUI.buttonAction(deluge::hid::button::X_ENC, true) == ActionResult::NOT_DEALT_WITH).to_be_true();
	});

	it("leaves menu view for the keyboard on KEYBOARD", _ {
		// `SoundEditor::buttonAction`'s KEYBOARD arm over the automation view.
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		automationView.onMenuView = true;
		automationView.previousUI = &instrumentClipView;
		fx.clip->onAutomationClipView = true;
		currentUIMode = UI_MODE_NONE;
		expect(shellHostUI.buttonAction(deluge::hid::button::KEYBOARD, true) == ActionResult::DEALT_WITH).to_be_true();
		expect(getRootUI() == &keyboardScreen).to_be_true();
		expect(automationView.onMenuView).to_be_false();
		expect(fx.clip->onAutomationClipView).to_be_false();
		expect(menu_scene().menu_context().root_is_keyboard).to_be_true();
	});

	it("leaves the keyboard's and the clip view's KEYBOARD arms to the menu", _ {
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		currentUIMode = UI_MODE_NONE;
		expect(shellHostUI.buttonAction(deluge::hid::button::KEYBOARD, true) == ActionResult::NOT_DEALT_WITH)
		    .to_be_true();
		expect(getRootUI() == &instrumentClipView).to_be_true();
	});

	it("in menu view, swaps back on a shortcut and on a move to another param, and stays on BACK out", _ {
		// `Automation::buttonAction`'s SELECT arm in menu view returns to the UI CLIP left, for a shortcut (even to the
		// same param) and any move onto a param; BACK out of the menu sends nothing (copied quirk).
		{
			Rig r;
			automationView.onMenuView = true;
			automationView.previousUI = &instrumentClipView;
			r.open(nodes::sound_filters);
			r.shortcut(8, 7);
			expect(getRootUI() == &instrumentClipView).to_be_true();
			expect(automationView.onMenuView).to_be_false();
		}
		{
			Rig r;
			automationView.onMenuView = true;
			automationView.previousUI = &instrumentClipView;
			r.open(nodes::sound_filters);
			r.press(kSelectEncButtonX, kSelectEncButtonY); // onto MODE, no param: nothing
			expect(automationView.onMenuView).to_be_true();
			r.press(kKitX, kKitY); // onto frequency
			expect(getRootUI() == &instrumentClipView).to_be_true();
		}
		{
			Rig r;
			automationView.onMenuView = true;
			automationView.previousUI = &instrumentClipView;
			r.open(nodes::sound_filters);
			r.press(kSelectEncButtonX, kSelectEncButtonY);
			r.press(kBackX, kBackY);
			r.press(kBackX, kBackY);
			expect(r.shell.stack().depth()).to_equal(size_t(0));
			expect(getRootUI() == &automationView).to_be_true();
			expect(automationView.onMenuView).to_be_true();
		}
		automationView.onMenuView = false;
	});
});
describe automation_encoders("the knobs and encoders under the menu", $ {
	it("turns a gold knob into the view's param in its editor", _ {
		// `SoundEditor::modEncoderAction` hands the turn to the view, which edits its selected param (HPF frequency, no
		// knob's) rather than the knob's own.
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 9, 7)).to_be_true();
		view.setActiveModControllableTimelineCounter(fx.clip); // as the clip view leaves it
		fx.clip->lastSelectedParamKind = params::Kind::PATCHED;
		fx.clip->lastSelectedParamID = params::LOCAL_HPF_FREQ;
		expect(deluge::gui::ui::automationEditorTakesKnobs()).to_be_true();
		const int32_t before = fx.clip->paramManager.getPatchedParamSet()->getValue(params::LOCAL_HPF_FREQ);
		shellHostUI.modEncoderAction(1, 8);
		expect(fx.clip->paramManager.getPatchedParamSet()->getValue(params::LOCAL_HPF_FREQ) != before).to_be_true();
	});

	it("leaves the gold knobs to the menu's knobs on the view's overview", _ {
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 9, 7)).to_be_true();
		view.setActiveModControllableTimelineCounter(fx.clip); // as the clip view leaves it
		expect(deluge::gui::ui::automationEditorTakesKnobs()).to_be_false();
		const int32_t before = fx.clip->paramManager.getPatchedParamSet()->getValue(params::LOCAL_HPF_FREQ);
		shellHostUI.modEncoderAction(1, 8);
		expect(fx.clip->paramManager.getPatchedParamSet()->getValue(params::LOCAL_HPF_FREQ)).to_equal(before);
	});

	it("passes a vertical turn to the root, and keeps it with SHIFT held", _ {
		// `SoundEditor::verticalEncoderAction`: the root's, unless SHIFT or X_ENC is held.
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		currentUIMode = UI_MODE_NONE;
		expect(open_shortcut_over(&instrumentClipView, fx.clip, 8, 7)).to_be_true();
		const int32_t before = fx.clip->yScroll;
		(void)shellHostUI.verticalEncoderAction(1);
		expect(fx.clip->yScroll != before).to_be_true();
		const int32_t moved = fx.clip->yScroll;
		{
			spec::ShiftHeld shift;
			(void)shellHostUI.verticalEncoderAction(1);
		}
		expect(fx.clip->yScroll).to_equal(moved);
	});
});
// clang-format on

DELUGE_SPEC(automation_retarget_spec, automation_root_facts, automation_retarget, automation_menu_view,
            automation_encoders)
