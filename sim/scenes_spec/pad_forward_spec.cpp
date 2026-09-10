/// @file
/// @brief `gui::ui::forwardGridPad` -- `SoundEditor::padAction`'s root switch
///        (`gui/ui/sound_editor.cpp:1474-1517`) and its tail (`:1520-1545`), ported.
///
/// What this pins: the six-way decision `forwardGridPad` makes over the real legacy root-UI
/// singletons (`keyboardScreen`, `instrumentClipView`, `automationView`, `performanceView`, and the
/// tail's fallback), through a recording `perform` installed by `setPadForwardPerform` -- so an
/// example can assert the decision, and the coordinates the target would have been handed, without
/// running a legacy view's own pad handler. Two exceptions run for real instead of through the
/// recorder: "really removes the ported menu when the tail fires" restores the real `perform` and
/// runs the real tail arm (`closePortedMenu`) against a real `MenuScene` on the real guest's scene
/// stack -- that call names no legacy view, so it is safe to run for real here, and it is what makes
/// behaviour 3 (the tail firing `closePortedMenu`) covered outside the recorder; the four
/// `automationView` examples open a real `MenuScene` (via `open_menu_node`, on `menu_scene()`
/// directly rather than on the guest's stack) because `menuShowsAutomationParam` reads
/// `menu_scene().focused()` -- there is no other way to give it an answer.
///
/// What this deliberately does not cover: the tail's firmware-version diagnostics, out of scope for
/// this switch -- `shell_pad_forward.h`'s own note says why. And nothing here asks
/// `Scene::grid_pad_claim()` (Task 1): that question is asked by this function's caller, a later
/// task, before `forwardGridPad` is ever reached.

#include "gui/ui/shell_pad_forward.h"

#include "definitions_cxx.hpp" // kDisplayWidth
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_host_ui.h" // shellHostUI
#include "gui/ui/ui.h"            // setRootUILowLevel, nullifyUIs, currentUIMode, UI_MODE_*
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"

#include "modulation/params/param.h" // params::Kind, LOCAL_LPF_FREQ, UNPATCHED_BASS -- task 3's param nodes
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h" // SceneKind

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::gui::ui::PadForwardTarget;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
namespace nodes = deluge::scenes::menu::nodes;
namespace params = deluge::modulation::params;

namespace {

/// @brief Put the legacy UI hierarchy back to "nothing open" after an example seats a root with
///        `setRootUILowLevel`. `menu_actions_spec.cpp`'s `UiStackGuard`, unchanged: `nullifyUIs()`
///        is what legacy itself calls to tear the hierarchy down (`gui/ui/ui.cpp:257-261`), and
///        `closeUI` is not safe here for the same `[-1]` reason that file's copy of this note gives.
struct UiStackGuard {
	UiStackGuard(const UiStackGuard&) = delete;
	UiStackGuard& operator=(const UiStackGuard&) = delete;
	UiStackGuard() = default;
	~UiStackGuard() { nullifyUIs(); }
};

/// @brief Empty the GLOBAL guest's scene stack, whichever way the one example that touches it ends.
///        `menu_actions_spec.cpp`'s `GlobalGuestGuard`, unchanged: `shellHostUI` is a singleton with
///        one open site (`gui/ui/shell_menu_entry.cpp:191`), and `closePortedMenu` reads that same
///        global (`gui/ui/shell_action_dispatch.cpp`), so an example proving it empties the stack has
///        to use the real one.
struct GlobalGuestGuard {
	GlobalGuestGuard(const GlobalGuestGuard&) = delete;
	GlobalGuestGuard& operator=(const GlobalGuestGuard&) = delete;
	GlobalGuestGuard() = default;
	~GlobalGuestGuard() {
		while (shellHostUI.shell().stack().depth() > 0) {
			shellHostUI.shell().stack().pop(SceneKind::Null);
		}
	}
};

/// @brief Open a real `MenuScene` at `nodes::root_song` on the global guest's OWN scene stack --
///        `menu_actions_spec.cpp`'s `open_global_guest`, narrowed to the one root this file needs.
///
/// `root_song` needs no clip (`context_for`'s second argument is `nullptr`), so this needs nothing
/// from `spec::SongFixture` but the `Song&` itself -- unlike `sound_root`, which task 1's spec fixture
/// resolves a clip and instrument for.
///
/// No `openUI` here, deliberately, matching `open_global_guest`'s own note: this only needs the menu
/// on the guest's *scene* stack, not the guest on `uiNavigationHierarchy` -- the root UI for the one
/// example that calls this is seated separately, with `setRootUILowLevel`.
bool open_menu_on_global_guest(Song& song) {
	const MenuContext ctx = menu::context_for(song, nullptr, 0, false,
	                                          /*session_or_arranger_root=*/true, /*session_view_root=*/true,
	                                          /*kit_global_fx=*/false);
	static const Node* const roots[] = {&nodes::root_song};
	(void)menu::assign_node_ids(roots);
	menu_scene().open(nodes::root_song, ctx);
	shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::root_song)});
	return shellHostUI.opened();
}

/// @brief `menu_scene_spec.cpp`'s `Rig::open`, narrowed to a bare node rather than a root: the
///        four `menuShowsAutomationParam` examples below need `menu_scene().focused()` to answer a
///        specific node, not a working menu tree, so this skips `assign_node_ids` and the shell
///        stack entirely -- none of them asks `Scene::grid_pad_claim()` of these nodes.
void open_menu_node(Song& song, InstrumentClip* clip, const Node& node) {
	const MenuContext ctx = menu::context_for(song, clip, 0, false, /*session_or_arranger_root=*/false,
	                                          /*session_view_root=*/false, /*kit_global_fx=*/false);
	menu_scene().open(node, ctx);
}

/// @brief A `Kind::Toggle` leaf: `bind.param` stays default-constructed (`ParamRef{}`, invalid), so
///        this is "a node with no param binding" for the sidebar example.
bool g_toggle_state = false;
constexpr Node no_param_node = menu::toggle_item(
    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ENABLED),
    [](const MenuContext&) { return static_cast<int32_t>(g_toggle_state); },
    [](const MenuContext&, int32_t v) { g_toggle_state = v != 0; });

/// @brief An ordinary patched param -- `bindings_spec.cpp`'s `lpf_freq_node`. Used both as the
///        "matches" node (paired with a clip set to the same kind/index) and the "does not match"
///        one (paired with a clip left at its default `Kind::NONE` / `kNoParamID`, or explicitly
///        set to a different param).
constexpr Node param_node = menu::param(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_FREQUENCY),
                                        {params::Kind::PATCHED, params::LOCAL_LPF_FREQ});

} // namespace

// clang-format off
describe pad_forward("gui::ui::forwardGridPad -- SoundEditor::padAction's root switch", $ {
	// The recording perform. `forwardGridPad` returns its decision too; the recorder exists so an
	// example can also assert the coordinates the target would have been handed.
	static PadForwardTarget seen;
	static int32_t seen_x, seen_y, seen_v;

	before_each([] {
		seen = PadForwardTarget::None;
		deluge::gui::ui::setPadForwardPerform([](PadForwardTarget t, int32_t x, int32_t y, int32_t v) {
			seen = t; seen_x = x; seen_y = y; seen_v = v;
		});
	});
	after_each([] { deluge::gui::ui::setPadForwardPerform(nullptr); });

	it("sends every pad to the keyboard screen when that is the root", _ {
		// `sound_editor.cpp:1474-1477`: the arm has no coordinate test at all.
		UiStackGuard ui_guard;
		setRootUILowLevel(&keyboardScreen);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Keyboard);
		expect(seen_x).to_equal(3);
	});

	it("sends the audition column, and only that column, to the clip view", _ {
		// `:1499-1502`: `x == kDisplayWidth + 1`, nothing else.
		UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64))
		    .to_equal(PadForwardTarget::AuditionColumn);
	});

	it("lets a main-grid pad on a clip root fall through to the tail", _ {
		// `:1500`'s `else if` not taken -> the comment `// fall through below` at `:1504` -> the
		// tail at `:1520-1545`. M3: this is NOT the note-editor arms' gap -- neither `inNoteEditor()`
		// nor `inNoteRowEditor()` is true here (no guest path ever sets that state, so they can't
		// be), and design spec §2.1 (`docs/.../2026-09-09-guest-pad-forwarding-design.md:104-106`)
		// says those two arms are exactly the ones that are **not** reachable with the toggle on,
		// for that reason -- nothing ships from an arm nothing can reach. This example is ordinary
		// behaviour 3 (§2.1) at site 5/one of site 6's fan-out arms: `instrumentClipView` matches no
		// coordinate-gated arm above it for a plain main-grid pad, so both legacy and the port fall
		// to the tail the same way -- no divergence in this example.
		UiStackGuard ui_guard;
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("sends everything to the performance view when that is the root", _ {
		// `:1515-1517`, which returns performanceView's own result and never reaches the tail.
		UiStackGuard ui_guard;
		setRootUILowLevel(&performanceView);
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::Performance);
	});

	it("does not exit on a release", _ {
		// `:1520`'s `&& on`. Load-bearing: a swallowed release leaves legacy's audition state
		// latched after a forwarded audition-column press.
		UiStackGuard ui_guard;
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 0)).to_equal(PadForwardTarget::None);
	});

	it("does not exit while a UI mode is active", _ {
		// `:1520`'s `currentUIMode == UI_MODE_NONE`.
		UiStackGuard ui_guard;
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(deluge::gui::ui::forwardGridPad(3, 4, 64)).to_equal(PadForwardTarget::None);
		currentUIMode = UI_MODE_NONE;
	});

	it("does not exit on the three firmware-version diagnostic pads (I2)", _ {
		// `sound_editor.cpp:1521-1543`: while `&firmwareVersionMenu` is open, `y == 7` and `x` in
		// {13, 14, 15} return `DEALT_WITH` **without** falling to `exitCompletely()` -- the popups
		// themselves stay unported (`shell_pad_forward.h`'s own note), but the "menu stays open"
		// half is legacy's own behaviour and is what this pins. `open_menu_node` focuses the ported
		// menu on `nodes::firmware_version` directly, the same shape the automation examples above
		// use for their own node.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		open_menu_node(*fx.song, fx.clip, nodes::firmware_version);
		setRootUILowLevel(&sessionView); // any root with no matching forwarding arm
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(15, 7, 64)).to_equal(PadForwardTarget::None); // SRAM
		expect(deluge::gui::ui::forwardGridPad(14, 7, 64)).to_equal(PadForwardTarget::None); // voices
		expect(deluge::gui::ui::forwardGridPad(13, 7, 64)).to_equal(PadForwardTarget::None); // PIC ver

		// Control: a pad this carve-out does not name, on the same open menu, still exits -- the
		// clause is additive, not a blanket "firmware-version menu never exits".
		expect(deluge::gui::ui::forwardGridPad(12, 7, 64)).to_equal(PadForwardTarget::Exit);
		expect(deluge::gui::ui::forwardGridPad(15, 6, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("does exit on the same three pads when a different node is focused", _ {
		// The carve-out is keyed on node identity, not on coordinate alone -- without
		// `onFirmwareVersionMenu()`'s check, any menu open at `y == 7` would swallow these three
		// pads regardless of what it was showing.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		open_menu_node(*fx.song, fx.clip, no_param_node);
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_NONE;

		expect(deluge::gui::ui::forwardGridPad(15, 7, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("really removes the ported menu when the tail fires", _ {
		// The one example that runs the REAL perform: `closePortedMenu()` names no legacy view, so
		// it is safe here, and this is what makes behaviour 3 covered outside the recorder.
		deluge::gui::ui::setPadForwardPerform(nullptr);
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_menu_on_global_guest(*fx.song)).to_be_true();
		expect(shellHostUI.shell().stack().depth() > 0u).to_be_true();

		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		(void)deluge::gui::ui::forwardGridPad(3, 4, 64);
		expect(shellHostUI.shell().stack().depth()).to_equal(0u);
	});

	it("sends a sidebar pad to automation view regardless of the param", _ {
		// `:1553-1557`: `(x >= kDisplayWidth) || editingParamInAutomationView` -- the sidebar half
		// needs no param agreement at all.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		open_menu_node(*fx.song, fx.clip, no_param_node);
		setRootUILowLevel(&automationView);
		expect(deluge::gui::ui::forwardGridPad(kDisplayWidth, 2, 64))
		    .to_equal(PadForwardTarget::Automation);
	});

	it("sends a main-grid pad to automation view when the menu shows the same param", _ {
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::PATCHED;
		fx.clip->lastSelectedParamID = params::LOCAL_LPF_FREQ;
		open_menu_node(*fx.song, fx.clip, param_node);
		setRootUILowLevel(&automationView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Automation);
	});

	it("falls through to the tail when the menu shows a different param", _ {
		// `handleAutomationViewPadAction` returns NOT_DEALT_WITH (`:1561`), and `:1508-1511` then
		// does not return -- so legacy reaches the tail.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.clip->lastSelectedParamKind = params::Kind::UNPATCHED_SOUND;
		fx.clip->lastSelectedParamID = params::UNPATCHED_BASS;
		open_menu_node(*fx.song, fx.clip, param_node);
		setRootUILowLevel(&automationView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Exit);
	});

	it("does not dereference a null clip", _ {
		// The guard migration §10.5 banked for legacy's copy (`:1582-1586` dereferences
		// `getCurrentClip()` unguarded). This is the port's own copy, so writing the guard here is
		// not the banked fix -- legacy's copy is untouched.
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		automationView.onArrangerView = false;
		fx.song->setCurrentClip(nullptr); // no current clip -- `getCurrentClip()` returns nullptr
		open_menu_node(*fx.song, fx.clip, param_node);
		setRootUILowLevel(&automationView);
		currentUIMode = UI_MODE_NONE;
		expect(deluge::gui::ui::forwardGridPad(3, 2, 64)).to_equal(PadForwardTarget::Exit);
	});
});
// clang-format on

DELUGE_SPEC(pad_forward_spec, pad_forward)
