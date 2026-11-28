/// @file
/// @brief The **action seam** (`ui/scenes/menu/action.h`) and the five items that use it, driven
///        through a real `Shell` over the real menu tables.
///
/// What is covered here, and what is not, matters more than usual for this spec, because the seam
/// deliberately splits one legacy statement in half:
///
///   * **Covered:** that pressing select on each of the five items files exactly one
///     `MenuActionRequest`, with the right *identity* and the right payload; that the four pure
///     verbs never descend into a screen of their own (legacy's `shouldEnterSubmenu() == false`);
///     that `PerformEditorItem` renders legacy's own option list, seeds its cursor from the mode,
///     and reproduces both of `selectButtonPress`'s two return values; that nothing files an action
///     without a press, across quiet ticks; and that the five item-to-action pairings are pinned by
///     identity, with a permutation proof that the pinning is not vacuous.
///   * **Covered in `menu_action_dispatch`:** that `gui::ui::dispatchShellActions`
///     is what takes a filed request out of the outbox. That is the consumer half's *seam*, not its
///     body.
///   * **Covered in `menu_action_open_perform`:** that `OpenPerformanceView` is
///     *performed* -- the two `performanceView` bools, the conditional `resetPerformanceView`, and
///     the `openUI`.
///   * **Covered in `menu_action_stem_export`:** the whole of `StartStemExport`'s
///     `getRootUI()` fan-out, driven by name through `stemExportTypeForRootUI()`, plus the arm end
///     to end down the one branch that starts nothing, plus the close.
///   * **Covered in `menu_action_three_verbs`:** that the remaining three verbs are
///     *performed* -- the clip's length really moves to the sample's, MIDI learn really arms, and
///     each context menu really lands on `uiNavigationHierarchy`.
///   * **Covered in `menu_action_screensaver`:** that both screensaver items move the legacy screensaver's
///     idle timer and the guest's `shell::Screensaver` idle period at the write, through the device port's store for
///     their rows, with no reboot.
///   * **NOT covered:** the acting half of `StartStemExport` -- `startStemExportProcess` would arm the recorder and
///     leave `UI_MODE_STEM_EXPORT` entered for an export job no spec drives (see that group's own note).
///
/// `PerformEditorItem`'s option list and its mapping from the performance view's two bools to a mode are compared
/// with the rows `tests/golden/ui/menu_oracle/menu_actions.tsv` holds for main's `EditingMode`; and the three one-line
/// verbs run their real callees: the audio clip scene under `audioClipView`, `sessionView` and the `gui/context_menu/`
/// singletons.
/// The identity pinning is kept alongside -- it is what says the right *item* files the right *action*, which no effect
/// assertion can say on its own.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/audio_clip/audio_clip_scene.h"
#include "ui/scenes/l10n_bridge.h"
#include "ui/scenes/menu/action.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/actions.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/walk.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "base/notify/notice_queue.h"
#include "model/edits/clip_address_for.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

// `card_access.h` before CppSpec, for the reason its own `@warning` gives.
#include "support/card_access.h"
#include "support/popup_capture.h"

#include "gui/context_menu/configure_song_macros.h"
#include "gui/context_menu/midi_learn_mode.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/root_ui.h" // PerformExitStubUI's base, for the arms that close()
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h" // isUIOpen / nullifyUIs / setRootUILowLevel, for the groups that use the real UI stack
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_root.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h" // commandToggleShift / clearShiftSticky, for the shift+KEYBOARD gestures
#include "hid/display/display.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/screensaver.h" // the legacy screensaver, half of what the screensaver setters move
#include "hid/led/pad_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h" // audioSampleTimer, the KEYBOARD release arm's clock
#include "processing/stem_export/stem_export.h"
#include "storage/flash_storage.h" // holdTime, the other half of that arm's gate

#include "support/global_guest_menu.h"
#include "support/host_flash.h"
#include "support/oracle_table.h"
#include "support/song_fixture.h"

#include "support/mocks/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::menu::MenuAction;
using deluge::ui::scenes::menu::MenuActionRequest;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace PadLEDs {
/// The pad dimmer's live level, which `pad_leds.h` does not declare (`hid/led/pad_leds.cpp`).
extern int32_t dimmerInterval;
/// The slow flash's tick squares as last set, 255 where none (`hid/led/pad_leds.cpp`).
extern uint8_t slowFlashSquares[];
/// Set when a column send waits for room in the PIC's queue (`hid/led/pad_leds.cpp`, `sendOutMainPadColoursSoon`).
extern bool needToSendOutMainPadColours;
} // namespace PadLEDs
/// The harness's record of every pad colour the app sent, column-major, eight rows of RGB
/// (`src/platform/device/src/peripherals/ui_harness.rs`).
extern "C" const uint8_t* host_script_pad_shadow();
extern uint32_t whichMainRowsNeedRendering; // `gui/ui/ui.cpp`: the main rows a render is pending for

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;

spec::OracleTable& table() {
	return spec::OracleTable::named("menu_actions");
}

/// @brief Per-example reset, exactly `menu_scene_spec.cpp`'s: pick a language (nothing boots here,
///        so `l10n::chosenLanguage` would otherwise be null) and drain the process-global notice
///        queue so one example's notification cannot put an overlay on the next one's stack.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
}

/// Board encoder index (`kDelugeEncoders`, `ui/shell/profiles/deluge.cpp`): 3 = Select.
constexpr uint8_t kSelectEncoder = 3;
/// The same table's 4 = `EncoderRole::GoldUpper` (`kDelugeEncoders`, `ui/shell/profiles/deluge.cpp`), which lowers to
/// `InputSnapshot::edit[0]` (`InputLowering::end_tick`). It does not move a `Kind::Int` leaf:
/// `int_value` claims the select turn instead (`ui/widgets/oled/values.cpp`'s `select_delta`),
/// as `render_leaf_int` records (`MenuView::render_leaf_int`, `ui/scenes/menu/view.cpp`).
constexpr uint8_t kGoldUpperEncoder = 4;
/// Matrix coordinates of the select-encoder button (`kMatrix`, `ui/shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

/// @brief The real roots this spec navigates, so `assign_node_ids` numbers everything it touches.
///
/// `root_settings` is here because the screensaver groups open its pages as a root, and `Rig::open`
/// needs `menu::node_id` to have numbered them. Adding a root here is safe for every other group --
/// ids are looked up per node, never assumed.
constexpr const Node* kRoots[] = {&nodes::root_song, &nodes::root_audio_clip, &nodes::root_performance,
                                  &nodes::root_settings};

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// `MenuScene` is a process singleton (`ui/shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `support/recorder_scene_fixtures.h`'s `~Rig` was written to stop (a leaked recorder there makes the next
	/// `on_enter` refuse outright and fails every later example). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	MenuContext open(const Node& root) {
		// The song root is opened from a root UI, not over a clip -- and that matters: `song_midi_learn` and
		// `configure_song_macros` carry `relevance::is_session_view`, so a clip context would list neither of the two
		// rows this spec presses. `SoundEditor::setup`'s `!rootUIIsClipMinderScreen()` arm
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) is the arm being stood in for; `sessionView` of its three
		// views, because that is the one where both items are shown.
		const bool song_root = &root == &nodes::root_song;
		const MenuContext ctx =
		    spec::ctx_of(*song.song, deluge::gui::menuViewOf(song_root ? nullptr : song.clip), 0, false,
		                 /*session_or_arranger_root=*/song_root,
		                 /*session_view_root=*/song_root, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	/// One detent of the (detented, 2 edges per detent) select encoder.
	void turn(int32_t detents, uint32_t at_ms) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2), at_ms); }
	/// One detent of the upper gold knob. **Not** doubled: the gold encoders are
	/// `EncoderKind::Accelerating`, whose lowering passes the raw edge count straight through
	/// (`InputLowering::apply_encoder_edges`), where `Detented` divides by `edges_per_detent`.
	void gold(int32_t detents, uint32_t at_ms) { edges(kGoldUpperEncoder, static_cast<int16_t>(detents), at_ms); }
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	void enter(uint32_t at_ms) {
		button(kSelectEncButtonX, kSelectEncButtonY, true, at_ms);
		button(kSelectEncButtonX, kSelectEncButtonY, false, at_ms);
	}
};

menu::MenuScene& scene() {
	return menu_scene();
}

/// @brief The real guest, over the real scene table -- what the firmware actually ticks.
///
/// `Rig` above is a bare `Shell` over `FakeSurfaceSet`, which is right for every example about
/// `MenuScene`. It is wrong for the one example about the *composition*, because the thing under
/// test there is `ShellHostUI::timerCallback` -- a function `Rig` does not contain. This holds the
/// real `ShellHostUI` instead, constructed the way `shell_host_ui.cpp`'s own global is: over
/// `scene_registry()`, `kDelugeBoard` and `deluge_profile()`.
///
/// **Not the `shellHostUI` global**, deliberately. `boot_wiring_spec.cpp` reads that object's shell
/// and its services, and an example that opened it, pushed a scene onto it and ticked it would be
/// handing every later spec in this binary a guest with a menu on its stack. A local one has its own
/// `Shell`, its own `GuestSurfaceSet` and its own input queue, and the only global it touches is the
/// `MenuScene` singleton -- which the destructor puts back, exactly as `Rig`'s does.
///
/// @note `opened()` calls the real `wireShellGuestServices()`, which installs the three services
///       into the **global** `shellHostUI`'s shell rather than into this one
///       (`wireShellGuestServices` in `gui/ui/shell_guest_services.cpp`). That is documented as idempotent, so
///       re-running it is a re-seed and not a second set; and it leaves this guest running on
///       `Services{}`, which is what `Rig` above runs on too. Nothing here reads a service.
struct GuestRig {
	spec::SongFixture song = spec::make_synth_song();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	deluge::gui::ui::ShellRuntime runtime{reg, deluge::ui::shell::kDelugeBoard, deluge_profile()};
	deluge::gui::ui::ShellHostUI ui{runtime};

	GuestRig(const GuestRig&) = delete;
	GuestRig& operator=(const GuestRig&) = delete;
	GuestRig() = default;

	/// Tear the stack down, so every scene's `on_exit` runs and the next example gets a fresh
	/// `MenuScene` -- `Rig::~Rig`'s reason, verbatim, and the same singleton.
	~GuestRig() {
		while (ui.shell().stack().depth() > 0) {
			ui.shell().stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	/// `Rig::open`'s two halves, through the guest's own door: point the scene at @p root with a
	/// session-view context, then let `opened()` do the `Shell::init` push.
	bool open(const Node& root) {
		const bool song_root = &root == &nodes::root_song;
		const MenuContext ctx =
		    spec::ctx_of(*song.song, deluge::gui::menuViewOf(song_root ? nullptr : song.clip), 0, false,
		                 /*session_or_arranger_root=*/song_root,
		                 /*session_view_root=*/song_root, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		ui.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ui.opened();
	}

	/// The select-encoder press, through the guest's own hook rather than the shell's -- which is
	/// the point: `buttonAction` only queues, and `Shell::pump_input` inside a tick is what routes
	/// (`Shell::pump_input`).
	void press_enter() {
		(void)ui.buttonAction(deluge::hid::button::SELECT_ENC, true);
		(void)ui.buttonAction(deluge::hid::button::SELECT_ENC, false);
	}

	/// One detent of the select encoder, through the guest's own hook. `ShellHostUI::selectEncoderAction`
	/// is the override legacy's `interpretEncoders` calls
	/// (`hid/encoder_input.cpp`'s `interpretEncoders`), and it queues into the same lowering `press_enter`
	/// does -- so, as there, nothing moves until the guest is ticked.
	void turn_select(int32_t detents) { ui.selectEncoderAction(static_cast<int8_t>(detents)); }
};

/// @brief @p parent's visible children in the order the scene lists them.
///
/// The scene's own `visible_children` is private, and this must agree with it or every index below
/// is off. It is the same two-line rule (`menu_scene.cpp`: resolve `dynamic_children` first, then
/// admit on `relevant`), and none of the parents this spec walks has a `dynamic_children` hook --
/// asserted, rather than assumed, at the top of `visible_index_of`.
std::vector<const Node*> visible_children_of(const Node& parent, const MenuContext& ctx) {
	std::vector<const Node*> out;
	for (const Node* child : parent.children) {
		if (menu::bindings::relevant(*child, ctx)) {
			out.push_back(child);
		}
	}
	return out;
}

/// @brief Where @p child sits in @p parent's visible list, or -1.
int32_t visible_index_of(const Node& parent, const Node& child, const MenuContext& ctx) {
	if (parent.dynamic_children != nullptr) {
		return -1; // none of this spec's parents has one; a table edit that adds one must be seen.
	}
	const std::vector<const Node*> kids = visible_children_of(parent, ctx);
	for (size_t i = 0; i < kids.size(); i++) {
		if (kids[i] == &child) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

/// @brief Open @p parent, walk the cursor onto @p child, and press select -- through the real
///        lowering, the real `list` widget and the real `enter_child` -- leaving whatever was filed
///        **in the outbox**.
///
/// `press_child` below is this plus the drain. Split so the groups where the dispatcher must be the
/// only reader of the outbox (`menu_action_open_perform` and after) can reuse the same walk instead
/// of a second copy of it; `press_perform_mode` is the one page whose payload varies and keeps its
/// own.
/// @return false when @p child is not visible in @p parent -- the case `press_child` reports as an
///         unengaged request.
bool press_child_leaving_request(Rig& r, const Node& parent, const Node& child) {
	const MenuContext ctx = r.open(parent);
	const int32_t index = visible_index_of(parent, child, ctx);
	if (index < 0) {
		return false;
	}
	// Settle the first frame, then walk. One detent per tick, which is what the widget's own
	// clamping walk expects.
	r.shell.tick(5);
	(void)scene().take_action(); // nothing may have been filed yet; assert that separately.
	for (int32_t i = 0; i < index; i++) {
		r.turn(1, static_cast<uint32_t>(10 + i * 10));
		r.shell.tick(static_cast<uint32_t>(15 + i * 10));
	}
	r.enter(static_cast<uint32_t>(100));
	r.shell.tick(static_cast<uint32_t>(110));
	return true;
}

/// @brief `press_child_leaving_request`, drained.
/// @return The action the scene filed as a result, drained exactly once.
MenuActionRequest press_child(Rig& r, const Node& parent, const Node& child) {
	if (!press_child_leaving_request(r, parent, child)) {
		return MenuActionRequest{};
	}
	return scene().take_action();
}

/// @brief The spec's transcription of main's `EditingMode::readCurrentValue`, the mapping the host's
///        `UiFacts::performance_editing_mode()` must answer.
///
/// Written here rather than read from the host, so the host's answer is compared with a second copy of the rule. The
/// "maps the performance view's two bools" example checks this copy against main's answers in the reference table.
PerformanceEditingMode mode_from_view() {
	if (!performanceView.defaultEditingMode) {
		return PerformanceEditingMode::DISABLED;
	}
	if (!performanceView.editingParam) {
		return PerformanceEditingMode::VALUE;
	}
	return PerformanceEditingMode::PARAM;
}

/// @brief Put `performanceView`'s two bools at the pair main's `EditingMode::selectButtonPress` leaves for @p mode
///        (DISABLED: both false), which is what `PerformEditorItem` then reads.
void set_view_mode(PerformanceEditingMode mode) {
	performanceView.defaultEditingMode = mode != PerformanceEditingMode::DISABLED;
	performanceView.editingParam = mode == PerformanceEditingMode::PARAM;
}

/// @brief Save and restore the two `performanceView` bools around an example that moves them.
struct PerformViewGuard {
	bool saved_default = performanceView.defaultEditingMode;
	bool saved_param = performanceView.editingParam;
	PerformViewGuard(const PerformViewGuard&) = delete;
	PerformViewGuard& operator=(const PerformViewGuard&) = delete;
	PerformViewGuard() = default;
	~PerformViewGuard() {
		performanceView.defaultEditingMode = saved_default;
		performanceView.editingParam = saved_param;
	}
};

/// @brief Put the legacy UI hierarchy back to "nothing open" after an example let the dispatcher
///        `openUI(&performanceView)`.
///
/// `nullifyUIs()` rather than `closeUI`, on `menu_entry_spec.cpp`'s terms: closing the only open UI is not a close
/// `closeUI` (`gui/ui/ui.cpp`) performs -- it returns early, where main's indexes
/// `uiNavigationHierarchy[u - 1]` with `u == 0` (`origin/main:src/deluge/gui/ui/ui.cpp`) -- and `nullifyUIs()` is what
/// legacy itself calls to tear the hierarchy down.
///
/// **Nothing is left behind.** With the stack empty, `getCurrentUI()` answers `emptyStackUI` (`gui/ui/ui.cpp`) and
/// `getRootUI()` reads only `numUIsOpen`. Main's `nullifyUIs()` instead keeps `lastUIBeforeNullifying`, which main's
/// `getCurrentUI()` answers on an empty hierarchy.
struct UiStackGuard {
	UiStackGuard(const UiStackGuard&) = delete;
	UiStackGuard& operator=(const UiStackGuard&) = delete;
	UiStackGuard() = default;
	~UiStackGuard() { nullifyUIs(); }
};

/// @brief Put `performanceView`'s two bools at the opposite of what a press for @p mode must leave
///        them at, and arm the reset marker.
///
/// Opposite on BOTH bools, so neither assertion below can pass on a value that was already there:
/// `defaultEditingMode` starts false and every branch sets it true; `editingParam` starts at
/// `!(mode == PARAM)`, which is what the branch for @p mode must move it away from.
void seed_view_away_from(PerformanceEditingMode mode) {
	performanceView.defaultEditingMode = false;
	performanceView.editingParam = (mode != PerformanceEditingMode::PARAM);
	// The reset marker. `resetPerformanceView` opens with `resetPadPressInfo()` (in
	// `PerformanceView::resetPerformanceView`), which is `initPadPress(firstPadPress)` + `initPadPress(lastPadPress)`
	// (`PerformanceView::resetPadPressInfo`), and `PerformanceView::initPadPress` clears `isActive`. `lastPadPress` is
	// the half of that pair the header makes public (`performance_view.h`).
	//
	// Nothing else on the dispatcher's path writes it: the only other writers in the whole view are
	// `padPressAction` (which sets `lastPadPress.isActive`), `padReleaseAction`'s `initPadPress`
	// calls and `restorePreviousHoldPress`, none of which
	// `openUI` -> `opened()` -> `focusRegained()` can reach. So `isActive` going false
	// means `resetPerformanceView` ran, and nothing else does.
	performanceView.lastPadPress.isActive = true;
}

/// @brief Open `perform_editor`, walk the cursor onto @p mode and press select, through the real
///        widget and the real lowering -- `press_child`'s walk, on the one page whose payload varies.
///
/// Leaves the filed request in the outbox **undrained**, deliberately: in this group the dispatcher
/// is the only thing allowed to read it.
void press_perform_mode(Rig& r, PerformanceEditingMode mode) {
	// The cursor starts at DISABLED, so the walk below is `mode` detents from 0: `PerformEditorItem::on_focus` seeds it
	// from the view, and the caller's `seed_view_away_from` leaves `defaultEditingMode` false, which is DISABLED.
	(void)r.open(nodes::perform_editor);
	r.shell.tick(5);
	for (int32_t i = 0; i < static_cast<int32_t>(mode); i++) {
		r.turn(1, static_cast<uint32_t>(10 + i * 10));
		r.shell.tick(static_cast<uint32_t>(15 + i * 10));
	}
	r.enter(100);
	r.shell.tick(110);
}

/// @brief Save and restore the `stemExport` fields the `StartStemExport` group reads or moves.
///
/// `exportMixdown` (`processing/stem_export/stem_export.h`) is what main's arranger arm branches on
/// (`Start::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/stem_export/start.h`) and is a user setting
/// elsewhere, so an example that moves it must put it back. `processStarted` and `currentStemExportType` are the first
/// two things `StemExport::startStemExportProcess` (`processing/stem_export/stem_export.cpp`) writes
/// (`currentStemExportType`, then `processStarted`), and are how the group below says that no export was started.
struct StemExportGuard {
	bool saved_mixdown = stemExport.exportMixdown;
	bool saved_started = stemExport.processStarted;
	StemExportType saved_type = stemExport.currentStemExportType;
	StemExportGuard(const StemExportGuard&) = delete;
	StemExportGuard& operator=(const StemExportGuard&) = delete;
	StemExportGuard() = default;
	~StemExportGuard() {
		stemExport.exportMixdown = saved_mixdown;
		stemExport.processStarted = saved_started;
		stemExport.currentStemExportType = saved_type;
	}
};

/// @brief Empty the **global** guest's scene stack, whichever way an example ended.
///
/// Its resting state in this binary is depth 0 -- `shellHostUI` is a static-init global that nothing
/// here opens except the one example below, and `SceneStack` starts empty -- so emptying it restores
/// exactly what was found. Popping unconditionally is `Rig::~Rig`'s discipline and the same
/// singleton is at stake: `MenuScene` left on any stack is what makes the next `on_enter` see stale
/// state.
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

/// @brief Open the menu at @p root on the ONE guest whose stack the dispatcher's close acts
///        on, over @p song's context -- `GuestRig::open`, aimed at the global instead of a local.
///
/// **This is the only thing in this file that touches `shellHostUI`, and it has to be.**
/// `closePortedMenu` removes the menu scene from `shellHostUI.shell().stack()` (see
/// `gui/ui/shell_action_dispatch.cpp`), because the guest is a singleton with one open site
/// (`gui/ui/menu_entry.cpp`) and this seam's sibling reads the same global for the same
/// kind of question (`menuCurrent`, `gui/ui/shell_guest_services.cpp`). A local
/// `GuestRig` has its own `Shell`, so its stack is not the one production empties -- an example
/// built on one could only ever assert that nothing happened to it.
///
/// The contamination `GuestRig`'s note warns about is the scene left behind, and `GlobalGuestGuard`
/// is what answers it: every example using this leaves the global at depth 0, the state it was
/// constructed in. `menu_entry_spec.cpp` drives the same global harder still, through
/// `openUI(&shellHostUI)`.
///
/// No `openUI` here, deliberately: this example needs the menu on the guest's *scene* stack, not the
/// guest on `uiNavigationHierarchy`. Keeping it off means no `closeUI(&shellHostUI)` can run in this
/// process, and so no root view's `focusRegained` either.
bool open_global_guest(const Node& root, spec::SongFixture& song) {
	const bool song_root = &root == &nodes::root_song;
	const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song_root ? nullptr : song.clip), 0, false,
	                                     /*session_or_arranger_root=*/song_root,
	                                     /*session_view_root=*/song_root, /*kit_global_fx=*/false);
	(void)menu::assign_node_ids(kRoots);
	menu_scene().open(root, ctx);
	shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(root)});
	return shellHostUI.opened();
}

/// @brief `open_global_guest`, but with the guest **on `uiNavigationHierarchy`** as well as on its
///        own scene stack -- so `getCurrentUI() == &shellHostUI`, the way production leaves it.
///
/// `open_global_guest` above deliberately keeps the guest off the UI stack, and says why. The
/// re-arm example below needs the opposite, and needs it for the thing under test: the guard in
/// `ShellHostUI::rearmTimer` is `getCurrentUI() != this`, so an example where the guest was never
/// current could not tell a working guard from a broken one -- it would take the early return in
/// both worlds and pass vacuously.
///
/// `openUI(&shellHostUI)` is production's own call (`gui/ui/menu_entry.cpp`) and
/// `menu_entry_spec.cpp` already drives the global this way. It runs `opened()` itself, so this
/// does NOT call `opened()` first the way `open_global_guest` does -- doing both would push the
/// initial scene twice.
///
/// @warning Seat a root UI first. `openUI` writes `uiNavigationHierarchy[numUIsOpen]` (`ui.cpp`),
///          so with an empty hierarchy the guest lands at index 0 and becomes `getRootUI()` -- the
///          precondition `stemExportTypeForRootUI`'s note in `gui/ui/shell_action_dispatch.cpp`
///          spells out. Teardown is `UiStackGuard`'s `nullifyUIs()`, never `closeUI`.
bool open_global_guest_on_ui_stack(const Node& root, spec::SongFixture& song) {
	const bool song_root = &root == &nodes::root_song;
	const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song_root ? nullptr : song.clip), 0, false,
	                                     /*session_or_arranger_root=*/song_root,
	                                     /*session_view_root=*/song_root, /*kit_global_fx=*/false);
	(void)menu::assign_node_ids(kRoots);
	menu_scene().open(root, ctx);
	shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(root)});
	return openUI(&shellHostUI);
}

/// @brief Leave `TimerName::UI_SPECIFIC` unset afterwards, whatever an example armed.
///
/// Unset rather than restored to its exact deadline: the timer is a shared one-shot whose resting
/// state between guest sessions is "not armed" (`ShellHostUI::rearmTimer`'s own no-deadline arm
/// leaves it alone), and every path that can wake the guest
/// re-arms it through `armTickSoon`. Leaving it armed would hand the next example a timer pointing
/// at a UI this one tore down.
struct UiSpecificTimerGuard {
	UiSpecificTimerGuard(const UiSpecificTimerGuard&) = delete;
	UiSpecificTimerGuard& operator=(const UiSpecificTimerGuard&) = delete;
	UiSpecificTimerGuard() = default;
	~UiSpecificTimerGuard() { uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC); }
};

/// @brief What `stemExportTypeForRootUI()` chose, as an `int`, with `-1` for "nothing".
///
/// One expectation per answer, and never `operator*` on the optional: `expect` does not abort the
/// example, so a two-step `has_value()` then `*chosen` turns a red assertion into a libstdc++
/// `_M_is_engaged()` abort that takes the rest of the group with it -- measured, on the first
/// mutation run of this group. `-1` is outside `StemExportType`'s four enumerators
/// (`definitions_cxx.hpp`), so "nothing" compares unequal to every real answer rather than
/// aliasing one.
int chosen_stem_export_type() {
	const std::optional<StemExportType> chosen = deluge::gui::ui::stemExportTypeForRootUI();
	return chosen.has_value() ? static_cast<int>(*chosen) : -1;
}

/// @brief Every item that files an action, paired with what it must file. Asserted by identity.
struct Pairing {
	const Node* node;
	const Node* parent;
	MenuAction action;
};

const std::array<Pairing, 4> kVerbs{{
    {&nodes::start_stem_export, &nodes::stem_export, MenuAction::StartStemExport},
    {&nodes::set_clip_length, &nodes::audio_clip_actions, MenuAction::SetClipLengthToSampleLength},
    {&nodes::song_midi_learn, &nodes::root_song, MenuAction::OpenSongMidiLearn},
    {&nodes::configure_song_macros, &nodes::root_song, MenuAction::OpenSongMacros},
}};

// --- the three one-line verbs --------------------------------------------------------

/// @brief One `MISC` action in an otherwise empty undo log, and an empty one again afterwards.
///
/// Not a convenience: main's `ClipView::lengthenClip` and `shortenClip`
/// (`origin/main:src/deluge/gui/views/clip_view.cpp`) read `actionLogger.firstAction[BEFORE]->type` **unguarded** in
/// their `PATTERN_PASTE` tests, so a length edit with an empty log segfaults there rather than failing. One inert
/// action puts both sides in the state the device is in by the time a length edit runs. `audio_clip_scene_spec.cpp`
/// picks the same `ActionType::MISC`, which no test in either function matches.
struct ActionLogGuard {
	ActionLogGuard(const ActionLogGuard&) = delete;
	ActionLogGuard& operator=(const ActionLogGuard&) = delete;
	ActionLogGuard() {
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
	}
	~ActionLogGuard() { actionLogger.deleteAllLogs(); }
};

/// @brief The `View` and UI-mode state `OpenSongMidiLearn` moves, saved and put back by hand.
///
/// By hand rather than by calling `sessionView.exitMidiLearnMode()`: that reaches
/// `View::endMIDILearn`, whose first statement can write the settings
/// file (`FlashStorage::writeSettings`) and whose fourth calls `getRootUI()->midiLearnFlash()` -- a
/// teardown that does more than undo.
///
/// `SessionView::gridModeActive` and `previousGridModeActive` are **private** (`session_view.h`,
/// in the class), so neither can be saved here nor asserted below; what an example after this one would
/// see of them is `SessionGridModeLaunch` and whatever it was. Stated rather than left to be found.
struct MidiLearnGuard {
	uint32_t saved_ui_mode = currentUIMode;
	bool saved_flash_on = view.midiLearnFlashOn;
	MidiLearn saved_pressed = view.thingPressedForMidiLearn;
	bool saved_should_save = view.shouldSaveSettingsAfterMidiLearn;
	MidiLearnGuard(const MidiLearnGuard&) = delete;
	MidiLearnGuard& operator=(const MidiLearnGuard&) = delete;
	MidiLearnGuard() = default;
	~MidiLearnGuard() {
		uiTimerManager.unsetTimer(TimerName::MIDI_LEARN_FLASH); // armed by `View::midiLearnFlash`
		view.shouldSaveSettingsAfterMidiLearn = saved_should_save;
		view.thingPressedForMidiLearn = saved_pressed;
		view.midiLearnFlashOn = saved_flash_on;
		currentUIMode = saved_ui_mode;
	}
};

/// @brief A `Sample` with a length and nothing else.
///
/// `audio_clip_scene_spec.cpp`'s `make_sample()` minus everything only the waveform reads (the cluster size and
/// the peak index): `SampleHolder::getLoopLengthAtSystemSampleRate` asks for
/// `getDurationInSamples(/*forTimeStretching=*/true)`, which is `endPos - startPos` and never touches the `Sample` at
/// all (`SampleHolder::getEndPos` in `model/sample/sample_holder.cpp`). The object still has to exist, because
/// `getLoopLengthAtSystemSampleRate`'s `if (audioFile)` is what picks that branch over "return the clip's current
/// length".
///
/// Placed in the deluge heap and never freed -- `support/song_fixture.h`'s own rule, for its reason.
Sample* make_length_only_sample(uint64_t length_in_samples) {
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = length_in_samples * 2;
	s->lengthInSamples = length_in_samples;
	return s;
}

/// Four seconds at 44.1kHz -- long enough that the length it implies is nowhere near
/// `make_audio_clip_song`'s default clip length, which is what makes the assertion below
/// non-vacuous.
constexpr uint64_t kSampleFrames = 44100 * 4;

/// @brief An audio song whose one clip carries `kSampleFrames` of sample, seated as `currentSong`.
///
/// @warning **Construct this AFTER the `Rig`**, or the `Rig`'s own `make_synth_song()` takes `currentSong` back and
///          the callee finds no audio clip to edit.
spec::AudioSongFixture seat_audio_clip_with_sample() {
	spec::AudioSongFixture f = spec::make_audio_clip_song();

	// The sample, attached the way `AudioClip::setupPlaybackBounds` leaves it: the whole file,
	// forwards. `guide.audioFileHolder` needs no assignment -- `AudioClip::AudioClip` points it at
	// `sampleHolder` permanently.
	f.clip->sampleHolder.audioFile = make_length_only_sample(kSampleFrames);
	f.clip->sampleHolder.startPos = 0;
	f.clip->sampleHolder.endPos = static_cast<int64_t>(kSampleFrames);
	// `SampleHolder::setAudioFile` normally computes this;
	// the fixture attaches by hand, so it is set by hand. `kMaxSampleValue` is the identity for
	// `getLengthInSamplesAtSystemSampleRate` -- and a **SIGFPE** at zero.
	f.clip->sampleHolder.neutralPhaseIncrement = kMaxSampleValue;

	// A realistic tempo, which `Song::setupDefault()` does not leave behind: a fresh song's
	// `project.transport.samples_per_timer_tick_big` makes `Song::getTimePerTimerTickFloat()` answer 2^32
	// (`model/song/song.cpp`), and `SampleHolder::getLoopLengthAtSystemSampleRate` divides by
	// `playbackHandler.getTimePerInternalTickFloat()`, which reads it (`model/sample/sample_holder.cpp`) -- so the
	// computed length would be **zero** and `adjustLoopLength` would write nothing while every assertion about "a popup
	// happened" still passed. `audio_clip_scene_spec.cpp` found that the hard way; this is its arithmetic, 120 BPM
	// through `Song::setBPMInner`'s own formula (`model/song/song.cpp`) rather than by calling it, because
	// `setTempoFromNumSamples` reaches a transport this process does not have.
	const double bpm = 120.0 * static_cast<double>(1u << f.song->project.transport.inside_world_tick_magnitude);
	f.song->project.transport.samples_per_timer_tick_big = static_cast<uint64_t>((110250.0 / bpm) * 4294967296.0);
	return f;
}

// --- the `PerformanceView` gestures that move its editing mode --------------------------

/// @brief A `RootUI` that claims nothing, seated UNDER `performanceView` so the two `buttonAction`
///        arms that `close()` have somewhere to close *to*.
///
/// `closeUI` walks down from `numUIsOpen - 1` while `u >= 1` and then reads `uiNavigationHierarchy[u - 1]` (main's
/// `closeUI` in `origin/main:src/deluge/gui/ui/ui.cpp`, the loop and then the read of the entry below), so closing the
/// only open UI indexes `[-1]` there -- the fact `UiStackGuard`'s note above records for `closeUI`; ours returns early
/// at `u < 1`. Seating this at index 0 with `openUI(&performanceView)` on top makes `close()` an in-bounds close onto
/// an object with no behaviour of its own; `focusRegained` is the only method `closeUI` calls on it and `UI` leaves it
/// empty. `midi_learn_spec.cpp`'s `StubRootUI` is the same object, built for the same reason.
class PerformExitStubUI final : public RootUI {
public:
	ActionResult buttonAction(deluge::hid::Button, bool) override { return ActionResult::DEALT_WITH; }
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

PerformExitStubUI& perform_exit_stub() {
	static PerformExitStubUI instance;
	return instance;
}

/// @brief Save and restore everything OUTSIDE the two bools that this group's gestures move.
///
/// `PerformViewGuard` above owns the two bools; this owns the rest of the state the view's gestures
/// are reached through. Each field is named with the site that needs it:
///
///   * `currentUIMode` -- the CLIP_VIEW arm tests it (`PerformanceView::buttonAction` in `performance_view.cpp`), and
///     `close()` and `changeRootUI` both unset `TimerName::UI_SPECIFIC` on the way past.
///   * `lastClipInstanceEnteredStartPos` -- picks which root a KEYBOARD exit changes to (in the KEYBOARD release arm),
///     and is what lets the CLIP_VIEW arm skip `transitionToViewForClip()` (the `== -1` test there).
///   * `sessionLayout` -- the grid arm of `padAction`.
///   * `affectEntire` -- `PerformanceView::focusRegained` writes it unconditionally, and
///     `openUI` runs that.
///   * `timeKeyboardShortcutPress` -- the KEYBOARD *release* arm's own gate (`PerformanceView::buttonAction`).
///   * `AudioEngine::audioSampleTimer` -- the other half of that gate's arithmetic.
///   * shift -- `Buttons::isShiftButtonPressed()` chooses between the KEYBOARD arm's two halves
///     (`PerformanceView::buttonAction`). Pressed with `commandToggleShift(true)` and released with
///     `Buttons::clearShiftSticky()`, which clears `shiftCurrentlyPressed` outright -- `commandToggleShift(false)`
///     would instead run the sticky-shift arithmetic (in `hid/buttons.cpp`) and can leave shift *stuck on* for the next
///     example.
struct PerformGestureGuard {
	uint32_t saved_ui_mode = currentUIMode;
	int32_t saved_entered_pos = currentSong->persistentView.lastClipInstanceEnteredStartPos;
	SessionLayoutType saved_layout = currentSong->persistentView.sessionLayout;
	bool saved_affect_entire = currentSong->persistentView.affectEntire;
	uint32_t saved_shortcut_press = performanceView.timeKeyboardShortcutPress;
	uint32_t saved_sample_timer = AudioEngine::audioSampleTimer;
	bool saved_shift = Buttons::isShiftButtonPressed();

	PerformGestureGuard(const PerformGestureGuard&) = delete;
	PerformGestureGuard& operator=(const PerformGestureGuard&) = delete;
	PerformGestureGuard() = default;
	~PerformGestureGuard() {
		if (!saved_shift) {
			Buttons::clearShiftSticky();
		}
		AudioEngine::audioSampleTimer = saved_sample_timer;
		performanceView.timeKeyboardShortcutPress = saved_shortcut_press;
		currentSong->persistentView.affectEntire = saved_affect_entire;
		currentSong->persistentView.sessionLayout = saved_layout;
		currentSong->persistentView.lastClipInstanceEnteredStartPos = saved_entered_pos;
		currentUIMode = saved_ui_mode;
	}
};

/// @brief Seat the stub root, open the real `performanceView` on top of it, and put the two bools at
///        @p default_mode / @p editing_param.
///
/// Every example asserts the fact answers that seed before its gesture, so no green after the gesture is a residue of
/// the example before.
void enter_perform_view(bool default_mode, bool editing_param) {
	currentUIMode = UI_MODE_NONE;
	setRootUILowLevel(&perform_exit_stub());
	(void)openUI(&performanceView);
	performanceView.defaultEditingMode = default_mode;
	performanceView.editingParam = editing_param;
}

/// @brief The host's `UiFacts::performance_editing_mode()` and `readCurrentValue`'s answer for the view as it
///        stands, as one value.
///
/// Both, deliberately, and each example compares the pair against a LITERAL mode: the two agreeing says nothing about
/// what they agree on, and stays green with `VALUE` and `PARAM` swapped on both sides. A free function rather than two
/// `expect` calls in a helper, because `expect` is `ItBase::expect` and only exists inside an example.
std::array<int, 2> fact_and_view() {
	return {static_cast<int>(deluge::gui::shell_host::host_ports().ui->performance_editing_mode()),
	        static_cast<int>(mode_from_view())};
}

/// @brief `fact_and_view()`'s answer when both halves are @p mode -- the expected value.
/// (`both` on its own is ambiguous against a name the spec headers already pull in.)
std::array<int, 2> both_at(PerformanceEditingMode mode) {
	return {static_cast<int>(mode), static_cast<int>(mode)};
}

/// @brief The `ScreensaverMode` values the ported enum item can hold, as the item's own cursor
///        indices.
///
/// They ARE the enum's numbers -- `enum class ScreensaverMode : uint8_t { OFF, BLANK, STARSCAPE, DELUGE }`
/// (`definitions_cxx.hpp`), and the `ui/screensaver/mode` row's choices (`project/device_settings.cpp`) list the
/// four in that order, as `ScreensaverModeMenu::getOptions`' own `@warning` requires
/// (`origin/main:src/deluge/gui/menu_item/defaults/screensaver.h`). Spelled as cursor indices because what the examples
/// drive is a cursor.
constexpr int32_t kModeOffIndex = 0;
constexpr int32_t kModeDelugeIndex = 3;

/// @brief Save and restore everything this group moves, and unset the legacy timer on the way out.
///
/// Three process globals plus one timer. The two `FlashStorage` fields and the **global**
/// `shellHostUI`'s idle period are the same three `boot_wiring_spec.cpp`'s `ScreensaverSettings`
/// guards, for the same reason: twenty-odd specs share this process. The fourth is
/// `TimerName::SCREENSAVER`, which `Screensaver::settingsChanged()` arms -- put back by unsetting it
/// rather than by restoring `Timer`'s two fields, because `UITimerManager::timeNextEvent` is a
/// private cache only `unsetTimer` recomputes (`gui/ui_timer_manager.cpp`). That is
/// `menu_entry_spec.cpp`'s teardown, verbatim.
struct ScreensaverGuard {
	ScreensaverMode mode = FlashStorage::screensaverMode;
	uint8_t minutes = FlashStorage::screensaverTimeoutMinutes;
	uint32_t idle_ms = shellHostUI.shell().screensaver_idle_ms();

	ScreensaverGuard(const ScreensaverGuard&) = delete;
	ScreensaverGuard& operator=(const ScreensaverGuard&) = delete;
	ScreensaverGuard() = default;

	~ScreensaverGuard() {
		FlashStorage::screensaverMode = mode;
		FlashStorage::screensaverTimeoutMinutes = minutes;
		shellHostUI.shell().set_screensaver_idle_ms(idle_ms);
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
	}
};

/// @brief What the **legacy** screensaver's idle timer is armed for, in milliseconds, or -1 while it
///        is unarmed.
///
/// `menu_entry_spec.cpp`'s helper, verbatim and for the same reason: `setTimer` stores
/// `AudioEngine::audioSampleTimer + ms * 44` (`gui/ui_timer_manager.cpp`), so the arm is read
/// back by undoing that. Nothing in this group advances the sample clock, so the read is exact rather
/// than approximate -- `menu_action_perform_fact` is the only group in this file that moves
/// `audioSampleTimer`, and it runs after all of these.
int32_t legacy_armed_ms() {
	const Timer& t = uiTimerManager.getTimer(TimerName::SCREENSAVER);
	return t.active ? static_cast<int32_t>(t.triggerTime - AudioEngine::audioSampleTimer) / 44 : -1;
}

/// @brief Put both settings at a known value and run legacy's own notifier, so an example starts from
///        a state where **both** screensavers already agree with flash.
///
/// `boot_wiring_spec.cpp`'s `idle_after`, plus the legacy timer that spec does not read. Seeding
/// through `settingsChanged()` rather than by assigning the globals is what makes the assertions
/// afterwards about a *change*: every example seeds at a value its answer is not.
void seed_screensaver(ScreensaverMode mode, uint8_t minutes) {
	FlashStorage::screensaverMode = mode;
	FlashStorage::screensaverTimeoutMinutes = minutes;
	deluge::hid::display::Screensaver::settingsChanged();
}

/// @brief Open the ported Screensaver page, step into @p child, and turn the encoder until the item
///        reads @p target -- leaving whatever the write filed **in the outbox**.
///
/// A real gesture: a detent walk
/// down the parent list to the wanted row, a row press to descend (neither `ScreensaverModeMenu` nor
/// `ScreensaverTimeout` overrides `shouldEnterSubmenu()`, so legacy enters both --
/// `MenuItem::shouldEnterSubmenu`), then one detent per tick. The write happens on the turn, where
/// legacy's does: `Value<T>::selectEncoderAction` calls `writeCurrentValue()`
/// (`origin/main:src/deluge/gui/menu_item/value.h`), which both items override.
///
/// @param enum_item true for `screensaver_mode` (whose cursor is `MenuState::enum_index`), false for
///        `screensaver_timeout` (whose value is `edit_int`).
/// @return The value the item ended on, or -1 if the row was not where the table says it is.
int32_t choose_screensaver(Rig& r, const Node& child, bool enum_item, int32_t target) {
	const MenuContext ctx = r.open(nodes::screensaver);
	r.shell.tick(5);
	(void)scene().take_action(); // opening the page files nothing; the group asserts that separately

	// `kScreensaverChildren` is `{&screensaver_mode, &screensaver_timeout}`
	// (`ui/scenes/menu/nodes/settings.h`), but the row is resolved rather than assumed -- a
	// `relevant` hook added to either would move it, and that must be seen and not walked past.
	const int32_t row = visible_index_of(nodes::screensaver, child, ctx);
	if (row < 0) {
		return -1;
	}
	for (int32_t i = 0; i < row; i++) {
		r.turn(1, 10 + static_cast<uint32_t>(i) * 10);
		r.shell.tick(15 + static_cast<uint32_t>(i) * 10);
	}
	r.enter(100);
	r.shell.tick(110);
	(void)scene().take_action(); // nor does descending into the leaf

	auto read = [enum_item] {
		return enum_item ? static_cast<int32_t>(scene().state().enum_index) : scene().state().edit_int;
	};
	int32_t value = read();
	for (uint32_t step = 0; value != target && step < 80; step++) {
		// **The same encoder for both.** `enum_value` owns the select encoder (`f.turn()`) and so does
		// `int_value` (`ui/widgets/oled/values.cpp`'s `select_delta`) -- exactly what legacy's own
		// `Value<T>::selectEncoderAction` does for both items.
		//
		// The loop still runs until the value lands rather than counting detents: in practice it lands in exactly
		// `abs(target - value)` iterations, but it makes no assumption about that.
		const int32_t d = target > value ? 1 : -1;
		r.turn(d, 120 + step * 10);
		r.shell.tick(125 + step * 10);
		value = read();
	}
	return value;
}

} // namespace

// clang-format off
describe menu_action_seam("scenes::menu action seam", $ {
	it("files nothing at all until something is pressed, across twenty quiet ticks", _ {
		select_language();
		Rig r;
		(void)r.open(nodes::stem_export);
		for (uint32_t t = 0; t < 20; t++) {
			r.shell.tick(10 + t * 10);
			expect(scene().take_action().engaged()).to_be_false();
		}
	});

	it("empties the outbox on the poll, so one press is not delivered twice", _ {
		select_language();
		Rig r;
		const MenuActionRequest first = press_child(r, nodes::stem_export, nodes::start_stem_export);
		expect(first.action).to_equal(MenuAction::StartStemExport);
		// Reading empties the slot -- a dispatcher that polls every tick must not see the same export
		// request four times.
		expect(scene().take_action().engaged()).to_be_false();
		r.shell.tick(200);
		expect(scene().take_action().engaged()).to_be_false();
		// **This is not an exactly-once guarantee, and the example is not claiming one.**
		// `take_action()` is a one-slot mailbox with a silent overwrite (see its warning): a caller
		// that skips a poll gets the request late, and two filed between polls would lose the first.
		// What is guaranteed, and what is checked here, is that a delivered request is delivered once.
	});

	it("survives a re-root: a request already accepted is still delivered", _ {
		select_language();
		Rig r;
		const MenuContext ctx = r.open(nodes::stem_export);
		r.shell.tick(5);
		r.enter(10);
		r.shell.tick(20);
		// `retarget` drops `MenuWalkState::pending_action`, but the drain has already moved this one into
		// the scene's own outbox, which it must not drop -- see `MenuScene::taken_action_`.
		menu_scene().open(nodes::root_song, ctx);
		expect(scene().take_action().action).to_equal(MenuAction::StartStemExport);
	});
});

describe menu_action_items("scenes::menu the four pure verbs", $ {
	it("each files its own action, and only its own", _ {
		select_language();
		for (const Pairing& p : kVerbs) {
			Rig r;
			const MenuActionRequest got = press_child(r, *p.parent, *p.node);
			expect(static_cast<int>(got.action)).to_equal(static_cast<int>(p.action));
			expect(static_cast<int>(got.arg)).to_equal(0);
		}
	});

	it("opened as a root, files NOTHING -- there was no press", _ {
		// **A deep link files nothing, and that is the point.** Filing the action on a `SceneId::arg` deep link onto
		// one of these items would be the port firing `selectButtonPress` with no press behind it. Main cannot: it
		// never navigates into these items at all (`shouldEnterSubmenu() == false`, `SoundEditor::buttonAction` in
		// `origin/main:src/deluge/gui/ui/sound_editor.cpp`), so the only way to reach the body is a press on the
		// parent's row, and `Toggle` -- the same shape -- guards the other way in explicitly
		// (`Toggle::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/toggle.h`).
		// `ValueSession::enter_leaf`'s `FocusCause` is that discriminator and `CustomItem::on_focus_is_press` is its
		// scope.
		//
		// A deep link onto one of these therefore shows the item's own title and does nothing --
		// which is still the one path `ActionItem::render` is reached by, so it is still pinned
		// here. Starting a stem export because a scene id was restored is not a defensible reading
		// of "put the user back where they were".
		select_language();
		Rig r;
		(void)r.open(nodes::start_stem_export);
		r.shell.tick(10);
		expect(scene().take_action().engaged()).to_be_false();
		expect(scene().focused()).to_equal(&nodes::start_stem_export);
		expect(static_cast<int>(scene().state().depth)).to_equal(0);
		// And it stays quiet: nothing re-fires on later ticks either.
		for (uint32_t t = 0; t < 5; t++) {
			r.shell.tick(20 + t * 10);
			expect(scene().take_action().engaged()).to_be_false();
		}
	});

	it("still files on a real press, so the guard is narrow rather than a mute button", _ {
		// The other side of the same discriminator, over all four items: a descent from the parent
		// list is `FocusCause::RowPress` and still acts. Without this the example above could be
		// satisfied by an item that never fires at all.
		select_language();
		for (const Pairing& p : kVerbs) {
			Rig r;
			const MenuActionRequest got = press_child(r, *p.parent, *p.node);
			expect(got.engaged()).to_be_true();
			expect(static_cast<int>(got.action)).to_equal(static_cast<int>(p.action));
		}
	});

	it("pins the item-to-action mapping by identity, and the pinning is not vacuous", _ {
		select_language();
		// The mapping under test, read off the shipped items rather than re-derived.
		std::vector<int> observed;
		for (const Pairing& p : kVerbs) {
			const auto* item = dynamic_cast<const nodes::ActionItem*>(p.node->custom);
			expect(item).not_().to_be_null();
			observed.push_back(item != nullptr ? static_cast<int>(item->action()) : -1);
		}
		std::vector<int> expected;
		for (const Pairing& p : kVerbs) {
			expected.push_back(static_cast<int>(p.action));
		}
		expect(observed).to_equal(expected);

		// ... and a single transposition of it must NOT compare equal, or the assertion above could
		// be passing on an all-`None` list. The pad-colour example carries the same proof.
		std::vector<int> swapped = observed;
		std::swap(swapped[0], swapped[2]);
		expect(swapped).not_().to_equal(expected);

		// The four actions are pairwise distinct, so "files the right one" is a real discriminator.
		std::set<int> distinct(observed.begin(), observed.end());
		expect(static_cast<int>(distinct.size())).to_equal(4);
	});

	it("never shows a screen of its own: the press leaves the parent list up", _ {
		select_language();
		for (const Pairing& p : kVerbs) {
			Rig r;
			const MenuActionRequest got = press_child(r, *p.parent, *p.node);
			expect(got.engaged()).to_be_true();
			// Main's `shouldEnterSubmenu() == false` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the item
			// is never navigated into. The port descends for the width of one `after_render` and
			// ascends inside it, so by the time the tick is over the parent is focused again.
			expect(scene().focused()).to_equal(p.parent);
			expect(static_cast<int>(scene().state().depth)).to_equal(0);
		}
	});
});

describe menu_action_perform("scenes::menu PerformEditorItem", $ {
	it("offers exactly EditingMode's options", _ {
		select_language();
		Rig r;
		(void)r.open(nodes::root_performance);

		// Main's list, frozen, and the port's: the names the item itself lists, in its order.
		const std::vector<std::string>* frozen = table().find("perform", "options");
		expect(frozen).not_().to_be_null();
		std::vector<std::string> port_names;
		for (const std::string_view name : nodes::PerformEditorItem::option_names()) {
			port_names.emplace_back(name);
		}
		expect(port_names).to_equal(frozen != nullptr ? *frozen : std::vector<std::string>{});
	});

	it("maps the performance view's two bools onto a mode exactly as main's readCurrentValue does", _ {
		select_language();
		PerformViewGuard guard;

		// All three reachable states of the pair, and the unreachable one: `{false, true}` is unreachable in main --
		// `readCurrentValue` tests `defaultEditingMode` first -- and maps to DISABLED under the same rule, so it is
		// asked rather than skipped.
		const std::array<std::pair<bool, bool>, 4> states{{{false, false}, {true, false}, {true, true}, {false, true}}};
		for (const auto& [default_mode, editing_param] : states) {
			performanceView.defaultEditingMode = default_mode;
			performanceView.editingParam = editing_param;

			const std::string path = "perform/" + std::to_string(int(default_mode)) + "," + std::to_string(int(editing_param));
			expect(path + " " + std::to_string(static_cast<int>(mode_from_view())))
			    .to_equal(path + " " + table().value(path, "mode"));
		}
	});

	it("seeds its cursor from the performance view's mode, as beginSession does", _ {
		select_language();
		PerformViewGuard guard;
		for (const auto mode :
		     {PerformanceEditingMode::DISABLED, PerformanceEditingMode::VALUE, PerformanceEditingMode::PARAM}) {
			Rig r;
			set_view_mode(mode);
			(void)r.open(nodes::perform_editor);
			r.shell.tick(10);
			expect(static_cast<int>(scene().state().list[0].cursor)).to_equal(static_cast<int>(mode));
			expect(scene().take_action().engaged()).to_be_false(); // opening files nothing
		}
	});

	it("a re-read takes the performance view's mode again, as EditingMode::readCurrentValue does", _ {
		select_language();
		PerformViewGuard guard;
		Rig r;
		set_view_mode(PerformanceEditingMode::DISABLED);
		(void)r.open(nodes::perform_editor);
		r.shell.tick(10);
		expect(static_cast<int>(scene().state().list[0].cursor)).to_equal(0);
		set_view_mode(PerformanceEditingMode::PARAM); // changed outside the menu
		scene().read_value_again();
		expect(static_cast<int>(scene().state().list[0].cursor)).to_equal(static_cast<int>(PerformanceEditingMode::PARAM));
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("files OpenPerformanceView carrying the chosen mode, and stays on the page", _ {
		select_language();
		for (const auto mode : {PerformanceEditingMode::VALUE, PerformanceEditingMode::PARAM}) {
			PerformViewGuard guard;
			Rig r;
			set_view_mode(PerformanceEditingMode::DISABLED);
			(void)r.open(nodes::perform_editor);
			r.shell.tick(5);
			for (int32_t i = 0; i < static_cast<int32_t>(mode); i++) {
				r.turn(1, static_cast<uint32_t>(10 + i * 10));
				r.shell.tick(static_cast<uint32_t>(15 + i * 10));
			}
			r.enter(100);
			r.shell.tick(110);

			const MenuActionRequest got = scene().take_action();
			expect(got.action).to_equal(MenuAction::OpenPerformanceView);
			expect(static_cast<int>(got.arg)).to_equal(static_cast<int>(mode));
			// The cursor stays on the chosen mode; the dispatcher, not the press, writes the view's two bools.
			expect(static_cast<int>(scene().state().list[0].cursor)).to_equal(static_cast<int>(mode));
			expect(static_cast<int>(mode_from_view())).to_equal(static_cast<int>(PerformanceEditingMode::DISABLED));
			// `NO_NAVIGATION` (`EditingMode::selectButtonPress`): main does not leave this page.
			expect(scene().focused()).to_equal(&nodes::perform_editor);
		}
	});

	it("goes up a level on DISABLED and files nothing, which is legacy's return nullptr", _ {
		select_language();
		PerformViewGuard guard;
		Rig r;
		set_view_mode(PerformanceEditingMode::VALUE);
		const MenuContext ctx = r.open(nodes::root_performance);
		const int32_t index = visible_index_of(nodes::root_performance, nodes::perform_editor, ctx);
		expect(index).to_equal(0);
		r.shell.tick(5);
		r.enter(10);
		r.shell.tick(20);
		expect(scene().focused()).to_equal(&nodes::perform_editor);
		expect(scene().take_action().engaged()).to_be_false();

		// Back to DISABLED, then press.
		r.turn(-1, 30);
		r.shell.tick(40);
		expect(static_cast<int>(scene().state().list[1].cursor))
		    .to_equal(static_cast<int>(PerformanceEditingMode::DISABLED));
		r.enter(50);
		r.shell.tick(60);

		expect(scene().take_action().engaged()).to_be_false();
		expect(scene().focused()).to_equal(&nodes::root_performance);
		// And the view is untouched: main's DISABLED arm writes nothing either.
		expect(static_cast<int>(mode_from_view())).to_equal(static_cast<int>(PerformanceEditingMode::VALUE));
	});

	it("files nothing across twenty quiet ticks while sitting on a non-DISABLED option", _ {
		select_language();
		PerformViewGuard guard;
		Rig r;
		set_view_mode(PerformanceEditingMode::PARAM);
		(void)r.open(nodes::perform_editor);
		for (uint32_t t = 0; t < 20; t++) {
			r.shell.tick(10 + t * 10);
			expect(scene().take_action().engaged()).to_be_false();
		}
	});
});

describe menu_action_dispatch("gui::ui::dispatchShellActions over the real outbox", $ {
	// The other side of the seam, in the binary that has every half: `deluge_ui_scenes_spec` links the
	// real `menu_scene()`, the real `dispatchShellActions` AND the real
	// `ShellHostUI::timerCallback` (`nm -C` on the driver shows all three as `T`). The host suite's
	// `tests/unit/deluge/gui/shell_action_dispatch_spec.cpp` links only the last of those -- it stubs the
	// dispatcher, because the real one belongs to the `deluge_ui_scenes` closure that suite never
	// builds -- so what it pins is the CALL SITE in isolation.
	//
	// Two isolated halves are not the composition, and the third example below is the one that says
	// so: a stub at the call site plus a separate proof that the real function reads the outbox does
	// not prove the real call site calls the real function. That example drives a real guest's own
	// `timerCallback()` and asserts the real outbox is empty afterwards.
	//
	// Nothing in THIS group asserts that anything is performed -- it is about the drain. Each arm's
	// effect has its own group below.

	it("takes the request a real item press filed, and leaves the outbox empty", _ {
		select_language();
		{
			// The control, and it is not decoration: without it the example below is satisfied by a
			// press that filed nothing at all, which is exactly how a drain test passes vacuously.
			Rig control;
			(void)control.open(nodes::stem_export);
			control.shell.tick(5);
			control.enter(10);
			control.shell.tick(20);
			expect(scene().take_action().action).to_equal(MenuAction::StartStemExport);
		}

		Rig r;
		(void)r.open(nodes::stem_export);
		r.shell.tick(5);
		r.enter(10);
		r.shell.tick(20);
		// Deliberately NOT drained by hand here -- the dispatcher is the only thing that reads it.
		deluge::gui::ui::dispatchShellActions();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("the file selector's opening leaves the keyboard's audition mode at once, and files nothing", _ {
		// `FileSelector::beginSession`: with the keyboard at the root and a note auditioning, opening the file selector
		// stops it before the browser opens; anywhere else it does nothing.
		select_language();
		UiStackGuard ui_guard;
		// The audition ends through the context's view actions port, the app's here.
		menu::MenuContext ctx{};
		ctx.ports = deluge::gui::shell_host::host_ports();

		setRootUILowLevel(&keyboardScreen);
		currentUIMode = UI_MODE_AUDITIONING;
		menu::MenuState state{};
		nodes::file0_selector.custom->on_focus(ctx, state);
		expect(currentUIMode).not_().to_equal(UI_MODE_AUDITIONING);
		expect(state.walk.pending_action.engaged()).to_be_false();

		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_AUDITIONING;
		menu::MenuState other{};
		nodes::file0_selector.custom->on_focus(ctx, other);
		expect(currentUIMode).to_equal(UI_MODE_AUDITIONING);
		currentUIMode = UI_MODE_NONE;
	});

	it("drops a request the closing guest's last input filed, so no later tick performs it", _ {
		// `ShellHostUI::closeGuest` feeds the input that arrived since the last tick before it closes. A select press
		// in it files its action on the way out, and the menu that would have acted is going.
		select_language();
		UiStackGuard ui_guard;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false,
		                                     /*session_or_arranger_root=*/false, /*session_view_root=*/false,
		                                     /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(nodes::stem_export, ctx);
		shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::stem_export)});
		setRootUILowLevel(&instrumentClipView);
		expect(openUI(&shellHostUI)).to_be_true();
		(void)shellHostUI.timerCallback(); // settle the first frame
		(void)scene().take_action();

		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true);
		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, false);
		shellHostUI.closeNow();

		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("takes nothing when nothing was filed, and does not eat the next request", _ {
		select_language();
		Rig r;
		(void)r.open(nodes::stem_export);
		r.shell.tick(5);
		// Twenty polls against an empty outbox -- which is what every quiet tick of the guest is.
		for (uint32_t t = 0; t < 20; t++) {
			r.shell.tick(10 + t * 10);
			deluge::gui::ui::dispatchShellActions();
		}
		// And the press after all of them still delivers, so the polling above left the seam intact
		// rather than jamming it.
		r.enter(300);
		r.shell.tick(310);
		expect(scene().take_action().action).to_equal(MenuAction::StartStemExport);
	});

	it("is reached BY the guest's own tick: press select, call timerCallback, outbox empty", _ {
		// The composition, end to end and with nothing stubbed: a real `ShellHostUI` over the real
		// `scene_registry()`, a real select press through the guest's own `buttonAction`, and the
		// guest's own `timerCallback()` -- which is the only thing this example calls that could
		// drain anything. `dispatchShellActions` is never named here.
		select_language();

		// The two legs differ in **one call** and share everything else: the same guest type over
		// the same registry, the same scene singleton, the same node, the same press through the
		// same input queue. Leg 1 ticks the SHELL directly (`guest.shell().tick`), which routes the
		// press and runs `MenuScene::after_render` but is not the guest's tick and so does not
		// poll; leg 2 ticks the GUEST (`timerCallback`), which does the same and then polls. So the
		// difference between "outbox full" and "outbox empty" isolates `timerCallback`'s own extra
		// work rather than comparing two different rigs.
		{
			GuestRig control;
			expect(control.open(nodes::stem_export)).to_be_true();
			control.ui.shell().tick(5);
			control.press_enter();
			control.ui.shell().tick(20);
			// The press really files, on this exact path, through this exact guest. Without this the
			// example below passes on a press that never filed anything.
			expect(scene().take_action().action).to_equal(MenuAction::StartStemExport);
		}

		GuestRig r;
		expect(r.open(nodes::stem_export)).to_be_true();
		(void)r.ui.timerCallback(); // settle the first frame
		expect(scene().take_action().engaged()).to_be_false(); // ...which files nothing on its own
		r.press_enter();
		(void)r.ui.timerCallback();
		expect(scene().take_action().engaged()).to_be_false();
	});
});

describe menu_action_screensaver("the screensaver items -- the device port's store", $ {
	// Both items write a DEVICE row (`ui/screensaver/mode`, `ui/screensaver/timeout`), whose store in the device port
	// sets its `FlashStorage` global and then calls main's `Screensaver::settingsChanged()`: the second statement of
	// both items' `writeCurrentValue`. That one call moves both screensavers, so every example
	// reads both: `shellHostUI.shell().screensaver_idle_ms()` is the guest's `shell::Screensaver` period and
	// `legacy_armed_ms()` is `TimerName::SCREENSAVER`, which the call re-arms, or unsets for `ScreensaverMode::OFF`.
	//
	// **The global `shellHostUI`, even where the rig has a guest of its own.** `settingsChanged()` names that object,
	// the firmware's one guest, so the global is what the call moves and what these examples read. `ScreensaverGuard`
	// puts it back.
	//
	// **Not covered: `settingsChanged()`'s wake-if-showing branch.** While the guest is the current UI,
	// `Screensaver::timerEvent()` stands the legacy screensaver down instead of activating it, so it cannot be showing
	// while a menu row is turned. `menu_entry_spec.cpp`'s `menu_entry_screensaver` group pins that stand-down.
	//
	// **Two items, one encoder.** `enum_value` and `int_value` both edit on the select turn (`ui/widgets/oled/values.cpp`'s
	// `select_delta`), as main reaches both items' `writeCurrentValue` from `Value<T>::selectEncoderAction`. `Rig`
	// feeds board encoder indices straight into a `Shell`; `ShellHostUI` routes a gold turn to the knobs' service rather
	// than to the menu, so the guest example turns the mode item.

	it("changing the timeout re-arms both screensavers at the write, with no reboot", _ {
		select_language();
		ScreensaverGuard guard;
		// Seeded at the value the answer is not, on both screensavers.
		seed_screensaver(ScreensaverMode::DELUGE, 5);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(300000));
		expect(legacy_armed_ms()).to_equal(int32_t(300000));

		Rig r;
		expect(choose_screensaver(r, nodes::screensaver_timeout, false, 9)).to_equal(int32_t(9));

		// Nine minutes, on both: `arm()`'s `screensaverTimeoutMinutes * kMillisecondsPerMinute` and the identical formula
		// the guest is handed. Nothing waits for a dispatcher.
		expect(static_cast<int>(FlashStorage::screensaverTimeoutMinutes)).to_equal(9);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(540000));
		expect(legacy_armed_ms()).to_equal(int32_t(540000));
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("the mode setter re-arms both the same way, and OFF cancels the legacy timer where a mode arms it", _ {
		// The timeout is held at 5 on every leg, so only the mode varies.
		select_language();
		ScreensaverGuard guard;

		// OFF -> DELUGE: both screensavers come on, at the timeout already stored.
		seed_screensaver(ScreensaverMode::OFF, 5);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(0));
		expect(legacy_armed_ms()).to_equal(int32_t(-1));
		{
			Rig r;
			expect(choose_screensaver(r, nodes::screensaver_mode, true, kModeDelugeIndex)).to_equal(kModeDelugeIndex);
		}
		expect(FlashStorage::screensaverMode).to_equal(ScreensaverMode::DELUGE);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(300000));
		expect(legacy_armed_ms()).to_equal(int32_t(300000));

		// ...and back to OFF, `settingsChanged()`'s other branch: the legacy timer is unset rather than re-armed, and the
		// guest spells the same thing as `idle_ms == 0`. Without this leg a call that only ever armed would pass.
		{
			Rig r;
			expect(choose_screensaver(r, nodes::screensaver_mode, true, kModeOffIndex)).to_equal(kModeOffIndex);
		}
		expect(FlashStorage::screensaverMode).to_equal(ScreensaverMode::OFF);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(0));
		expect(legacy_armed_ms()).to_equal(int32_t(-1));
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("does not ascend: the user is left standing on the item, as main leaves them", _ {
		// Main enters both items -- neither overrides `shouldEnterSubmenu()` -- and `writeCurrentValue` runs on every
		// detent while the user stands there, so leaving the page on the first turn would close it under them.
		select_language();
		ScreensaverGuard guard;
		seed_screensaver(ScreensaverMode::DELUGE, 5);

		Rig r;
		expect(choose_screensaver(r, nodes::screensaver_timeout, false, 7)).to_equal(int32_t(7));
		expect(legacy_armed_ms()).to_equal(int32_t(420000));
		expect(scene().focused()).to_equal(&nodes::screensaver_timeout);
		expect(static_cast<int>(scene().state().depth)).to_equal(1);

		// A second detent still lands, which leaving the page would have made impossible.
		r.turn(1, 200);
		r.shell.tick(210);
		expect(scene().state().edit_int).to_equal(int32_t(8));
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(480000));
		expect(legacy_armed_ms()).to_equal(int32_t(480000));
		expect(scene().focused()).to_equal(&nodes::screensaver_timeout);
	});

	it("re-arms nothing until the value is turned -- opening and descending are quiet", _ {
		// The other side of "a write, not a press". Walking onto the item and pressing into it is
		// what a user does before choosing anything, and it must not re-arm anybody's screensaver.
		select_language();
		ScreensaverGuard guard;
		seed_screensaver(ScreensaverMode::DELUGE, 5);
		const int32_t armed = legacy_armed_ms();

		Rig r;
		const MenuContext ctx = r.open(nodes::screensaver);
		r.shell.tick(5);
		expect(scene().take_action().engaged()).to_be_false();
		expect(visible_index_of(nodes::screensaver, nodes::screensaver_mode, ctx)).to_equal(int32_t(0));
		r.enter(10);
		r.shell.tick(20);
		expect(scene().focused()).to_equal(&nodes::screensaver_mode);
		expect(scene().take_action().engaged()).to_be_false();
		// ...and it stays quiet across twenty ticks with the item open.
		for (uint32_t t = 0; t < 20; t++) {
			r.shell.tick(30 + t * 10);
			deluge::gui::ui::dispatchShellActions();
			expect(scene().take_action().engaged()).to_be_false();
		}
		expect(FlashStorage::screensaverMode).to_equal(ScreensaverMode::DELUGE);
		expect(static_cast<int>(FlashStorage::screensaverTimeoutMinutes)).to_equal(5);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(300000));
		expect(legacy_armed_ms()).to_equal(armed);
	});

	it("reaches both screensavers through the guest's own input hook and tick", _ {
		// The composition end to end: a real `ShellHostUI` over the real `scene_registry()`, a real select-encoder turn
		// through the guest's own hook, and the guest's own `timerCallback()`. The rig's guest is a local `ShellHostUI`
		// (see `GuestRig`) and the object `settingsChanged()` pushes into is the global `shellHostUI`: in the firmware
		// they are one object, so here the local one routes the input and the global one records the result.
		//
		// The mode item, `OFF -> BLANK`: the change both screensavers show without touching the timeout, which stays at
		// 5 throughout.
		select_language();
		ScreensaverGuard guard;

		GuestRig r;
		seed_screensaver(ScreensaverMode::OFF, 5);
		expect(r.open(nodes::screensaver_mode)).to_be_true();
		(void)r.ui.timerCallback(); // settle the first frame
		expect(legacy_armed_ms()).to_equal(int32_t(-1));
		r.turn_select(1);
		(void)r.ui.timerCallback();

		expect(FlashStorage::screensaverMode).to_equal(ScreensaverMode::BLANK);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(300000));
		expect(legacy_armed_ms()).to_equal(int32_t(300000));
		expect(scene().take_action().engaged()).to_be_false();
	});
});

describe menu_action_open_perform("gui::ui::dispatchShellActions -- OpenPerformanceView", $ {
	// The first arm with a body, and the only group in this file that asserts a legacy
	// EFFECT rather than a filed request. It is the real dispatcher over the real `performanceView`
	// in the binary that links both, driven by a real select press on `nodes::perform_editor`.
	//
	// What each example must distinguish, and how:
	//
	//   * **the two bools** -- seeded at the opposite of the answer first (`seed_view_away_from`),
	//     so neither assertion can pass on a value that was already there;
	//   * **the reset** -- `performanceView.lastPadPress.isActive`, which only
	//     `resetPerformanceView` clears on this path (see `seed_view_away_from`'s note). This is the
	//     assertion the `!editingParam` guard is pinned by, and it is pinned in BOTH directions:
	//     VALUE must clear it and PARAM must leave it standing, so inverting the guard reddens both
	//     examples on the line that names the reset rather than on the line that names the open;
	//   * **the open** -- `isUIOpen(&performanceView)`, and NOT `getCurrentUI()`, which names only the top of the stack
	//     (`emptyStackUI` on an empty one, `gui/ui/ui.cpp`; main's answers `lastUIBeforeNullifying`,
	//     `origin/main:src/deluge/gui/ui/ui.cpp`) and so cannot say whether the view is open anywhere in the stack.
	//
	// Every example has a control leg that asserts the request really is sitting in the outbox, so no
	// subject's green can come from a press that filed nothing. What separates control from subject
	// is stated per example rather than as one claim, because it is not the same in all three:
	//
	//   * examples 1 and 2 -- EXACTLY ONE CALL, `dispatchShellActions()`. Same rig, same seed, same
	//     press, same ticks;
	//   * example 3 -- **one difference in kind, spread over three statements.** The control ticks
	//     the SHELL and the subject ticks the GUEST, at both of the two tick points; and the subject
	//     additionally drains once after its settle tick, asserting the outbox empty. That extra
	//     drain is not a leg the control has: it is there to say the settle tick files nothing on its
	//     own, and it is inert with respect to the assertions below because the press it would have
	//     to eat has not happened yet. "Shell tick vs guest tick" is the one thing being varied;
	//     three statements is what varying it honestly costs.

	it("VALUE: writes the two bools, resets the view, and opens it", _ {
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;

		{
			// Same rig, same press, same seed -- minus the dispatch.
			Rig control;
			seed_view_away_from(PerformanceEditingMode::VALUE);
			press_perform_mode(control, PerformanceEditingMode::VALUE);

			// Nothing happened, which is what makes the subject's assertions about the dispatch.
			expect(performanceView.defaultEditingMode).to_be_false();
			expect(performanceView.editingParam).to_be_true();
			expect(performanceView.lastPadPress.isActive).to_be_true();
			expect(isUIOpen(&performanceView)).to_be_false();
			// ...and the press really did file, so "nothing happened" is about the missing call and
			// not about a press that never reached the item.
			const MenuActionRequest filed = scene().take_action();
			expect(filed.action).to_equal(MenuAction::OpenPerformanceView);
			expect(static_cast<int>(filed.arg)).to_equal(static_cast<int>(PerformanceEditingMode::VALUE));
		}

		Rig r;
		seed_view_away_from(PerformanceEditingMode::VALUE);
		press_perform_mode(r, PerformanceEditingMode::VALUE);
		deluge::gui::ui::dispatchShellActions();

		// `EditingMode::selectButtonPress`
		// (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`) -- the VALUE branch.
		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_false();
		// `EditingMode::selectButtonPress` -- `!editingParam`, so the reset runs.
		expect(performanceView.lastPadPress.isActive).to_be_false();
		// `EditingMode::selectButtonPress` -- `openUI(&performanceView)`.
		expect(isUIOpen(&performanceView)).to_be_true();
		// And the outbox is empty, so the effects above came from this one request rather than from
		// a second delivery of it.
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("PARAM: writes both bools and opens the view, and does NOT reset it", _ {
		// Main's own reason, at `EditingMode::selectButtonPress`
		// (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`): resetting in param-editing
		// mode "would reset param assignments to FX columns". This is the half of the guard that an implementation
		// which simply always resets would still pass the VALUE example with.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;

		{
			Rig control;
			seed_view_away_from(PerformanceEditingMode::PARAM);
			press_perform_mode(control, PerformanceEditingMode::PARAM);

			expect(performanceView.defaultEditingMode).to_be_false();
			expect(performanceView.editingParam).to_be_false();
			expect(performanceView.lastPadPress.isActive).to_be_true();
			expect(isUIOpen(&performanceView)).to_be_false();
			const MenuActionRequest filed = scene().take_action();
			expect(filed.action).to_equal(MenuAction::OpenPerformanceView);
			expect(static_cast<int>(filed.arg)).to_equal(static_cast<int>(PerformanceEditingMode::PARAM));
		}

		Rig r;
		seed_view_away_from(PerformanceEditingMode::PARAM);
		press_perform_mode(r, PerformanceEditingMode::PARAM);
		deluge::gui::ui::dispatchShellActions();

		// `EditingMode::selectButtonPress`
		// (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`) -- the PARAM branch.
		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_true();
		// `EditingMode::selectButtonPress` -- `editingParam`, so the reset is SKIPPED and the marker stands.
		expect(performanceView.lastPadPress.isActive).to_be_true();
		// `EditingMode::selectButtonPress` -- the open happens either way; only the reset is conditional.
		expect(isUIOpen(&performanceView)).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("reaches the view through the guest's own tick, with nothing calling the dispatcher by name", _ {
		// The composition example, with an effect to see at the end of it: a real
		// `ShellHostUI` over the real `scene_registry()`, a real select press through the guest's own
		// `buttonAction`, and the guest's own `timerCallback()` -- which is the only thing here that
		// could open anything. `dispatchShellActions` is never named.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;

		// The two legs vary ONE THING -- shell tick vs guest tick, `GuestRig`'s own comment's
		// arrangement: the control ticks the SHELL (routes the press, runs `MenuScene::after_render`,
		// does not poll), the subject ticks the GUEST. It takes three statements rather than one,
		// and this is the whole list: both tick points differ, and the subject alone drains after its
		// settle tick to say that tick files nothing. That drain cannot affect what follows -- the
		// press it would have to eat is two lines later.
		// `GuestRig` has no encoder helper, so the cursor is placed by the view's mode instead of walked:
		// `PerformEditorItem::on_focus` seeds it from `UiFacts::performance_editing_mode()`,
		// which is `EditingMode::beginSession` -> `readCurrentValue`.
		// VALUE rather than DISABLED, because DISABLED is the arm that files nothing.
		{
			GuestRig control;
			set_view_mode(PerformanceEditingMode::VALUE);
			expect(control.open(nodes::perform_editor)).to_be_true();
			control.ui.shell().tick(5);
			seed_view_away_from(PerformanceEditingMode::VALUE);
			control.press_enter();
			control.ui.shell().tick(20);

			expect(performanceView.defaultEditingMode).to_be_false();
			expect(isUIOpen(&performanceView)).to_be_false();
			expect(scene().take_action().action).to_equal(MenuAction::OpenPerformanceView);
		}

		GuestRig r;
		set_view_mode(PerformanceEditingMode::VALUE);
		expect(r.open(nodes::perform_editor)).to_be_true();
		(void)r.ui.timerCallback(); // settle the first frame -- a render files nothing on its own
		expect(scene().take_action().engaged()).to_be_false();
		seed_view_away_from(PerformanceEditingMode::VALUE);
		r.press_enter();
		(void)r.ui.timerCallback();

		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_false();
		expect(performanceView.lastPadPress.isActive).to_be_false();
		expect(isUIOpen(&performanceView)).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});
});

describe menu_action_stem_export("gui::ui::dispatchShellActions -- StartStemExport", $ {
	// The second arm with a body, and the one `ui/scenes/menu/action.h` keeps as a single enumerator, because "the choice
	// is made from the root UI, which this layer cannot read". So the fan-out over `getRootUI()`
	// (`Start::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/stem_export/start.h`) is the whole of what
	// this side of the seam adds, and it is what this group pins.
	//
	// **Why the chooser is driven by name and the arm is not, which is a real limit and not a
	// preference.** `StemExport::startStemExportProcess` (`processing/stem_export/stem_export.cpp`) stops
	// playback, presses record, enters `UI_MODE_STEM_EXPORT` and requests the export job through
	// `deluge::runtime::appLoop` before it returns, and `StemExport::exportRoutine` then runs from the app loop. An
	// example that let a matching arm reach it would arm the recorder and leave `UI_MODE_STEM_EXPORT` entered
	// for a transport no spec is driving. So the
	// four matching arms are driven through `stemExportTypeForRootUI()`, and the dispatcher's arm is
	// driven end to end only where the chain falls through -- the branch that starts nothing.
	//
	// **The set these examples cover is the whole of main's chain**, stated so a reader can check it: `arrangerView`
	// twice (`Start::selectButtonPress` is one root with two answers), `sessionView` once, `instrumentClipView` twice
	// (the `&&` of its arm, once each way), plus the two ways to reach no arm at all -- a root main never names, and
	// no root open. Six root states, five answers, and every `if` in `Start::selectButtonPress`
	// (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`) has an example on both sides of it.
	//
	// Each example seeds `currentStemExportType` at a value the answer is not, so no assertion can
	// pass on a residue -- `StemExportGuard` puts it back.

	it("arrangerView with exportMixdown set chooses MIXDOWN", _ {
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_synth_song();

		// `setRootUILowLevel` is legacy's own "seat the root, run no
		// `opened()`" -- what `setupBlankSong` uses. `changeRootUI` would run the real
		// view's `opened()`, which is a great deal of view work for a fact the chooser answers with
		// one pointer comparison.
		setRootUILowLevel(&arrangerView);
		stemExport.exportMixdown = true;

		expect(chosen_stem_export_type()).to_equal(static_cast<int>(StemExportType::MIXDOWN)); // arranger, mixdown
	});

	it("arrangerView without exportMixdown chooses TRACK", _ {
		// The other half of the same `if` (the arranger arm without `exportMixdown`). Identical to the example above in
		// every statement but the one `exportMixdown` assignment, which is what makes the pair say that the flag is
		// read rather than that the arranger has a fixed answer.
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_synth_song();

		setRootUILowLevel(&arrangerView);
		stemExport.exportMixdown = false;

		expect(chosen_stem_export_type()).to_equal(static_cast<int>(StemExportType::TRACK)); // arranger, no mixdown
	});

	it("sessionView chooses CLIP", _ {
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_synth_song();

		// `exportMixdown` set, so this example also says the arranger's flag has no reach outside
		// the arranger arm -- an implementation that tested it before the root would answer MIXDOWN
		// here.
		setRootUILowLevel(&sessionView);
		stemExport.exportMixdown = true;

		expect(chosen_stem_export_type()).to_equal(static_cast<int>(StemExportType::CLIP)); // the session-view arm
	});

	it("instrumentClipView over a KIT chooses DRUM", _ {
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		// The output type is the second half of the instrument-clip arm's `&&`, and `getCurrentOutputType()`
		// reads it off the song's current clip (`model/song/song.cpp`) -- so the fixture is
		// what varies between this example and the one below, not the root.
		spec::SongFixture song = spec::make_kit_song();
		expect(getCurrentOutputType()).to_equal(OutputType::KIT);

		setRootUILowLevel(&instrumentClipView);

		expect(chosen_stem_export_type()).to_equal(static_cast<int>(StemExportType::DRUM)); // instrument clip, KIT
	});

	it("instrumentClipView over a non-KIT output chooses nothing, which is legacy's fall-through", _ {
		// The instrument-clip arm's `&&` in the direction that fails, and the end of `Start::selectButtonPress`: main
		// returns `NO_NAVIGATION` having started no export. Same root as the example above, different output -- the one
		// difference.
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_synth_song();
		expect(getCurrentOutputType()).to_equal(OutputType::SYNTH);

		setRootUILowLevel(&instrumentClipView);

		expect(chosen_stem_export_type()).to_equal(-1);
	});

	it("a root legacy never names, and no root at all, both choose nothing", _ {
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_kit_song();
		stemExport.exportMixdown = true;

		// A real `RootUI` that `Start::selectButtonPress` simply does not test for. A KIT song with
		// `exportMixdown` set, so both of the facts the other arms read are true here and neither
		// can carry this one on its own.
		setRootUILowLevel(&performanceView);
		expect(chosen_stem_export_type()).to_equal(-1);

		// And the null root: `getRootUI()` answers `nullptr` while `numUIsOpen == 0`
		// (`gui/ui/ui.cpp`), which is legacy's "during song load / swap" (the comment on `numUIsOpen`).
		// `nullifyUIs()` is how legacy itself gets there.
		nullifyUIs();
		expect(getRootUI()).to_be_null();
		expect(chosen_stem_export_type()).to_equal(-1);
	});

	it("the arm itself starts no export where legacy's chain falls through", _ {
		// The real `case MenuAction::StartStemExport:` end to end, driven by a real select press on
		// `start_stem_export` -- down the one branch a spec can take, where nothing is exported.
		select_language();
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		GlobalGuestGuard guest_guard; // the arm's close runs here too; see the example below

		{
			// The control, and it differs in EXACTLY ONE CALL: no `dispatchShellActions()`. Same
			// rig, same root, same fixture, same press, same ticks. Its job is to say the press
			// really files, so the subject's "nothing happened" is about the arm rather than about
			// a press that never reached the item.
			Rig control;
			setRootUILowLevel(&instrumentClipView);
			(void)control.open(nodes::stem_export);
			control.shell.tick(5);
			control.enter(10);
			control.shell.tick(20);
			expect(scene().take_action().action).to_equal(MenuAction::StartStemExport);
		}

		Rig r;
		// `Rig` builds its own `make_synth_song()`, so this root is the instrument-clip arm's `&&` failing.
		setRootUILowLevel(&instrumentClipView);
		expect(chosen_stem_export_type()).to_equal(-1);
		stemExport.processStarted = false;

		(void)r.open(nodes::stem_export);
		r.shell.tick(5);
		r.enter(10);
		r.shell.tick(20);
		deluge::gui::ui::dispatchShellActions();

		// Nothing was exported. `StemExport::startStemExportProcess` sets `processStarted`, so an arm that
		// started an export would have set it before this line.
		expect(stemExport.processStarted).to_be_false();
		// And the request is out of the outbox.
		//
		// **What this example cannot say, measured rather than assumed.** Commenting the whole
		// `startStemExport()` call out of the arm leaves it green: both assertions above hold of an
		// empty arm too, because the drain happens in `dispatchShellActions` before the `switch`
		// and the fall-through is legacy's own no-op. It is the example below, on the close, that
		// reddens when the arm's body goes away. That is the price of a verb whose acting half
		// cannot be run here, and it is why the close is pinned separately rather than trusted.
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("takes the menu scene off the guest's stack, which is exitCompletely()'s half", _ {
		// `soundEditor.exitCompletely()` (`Start::selectButtonPress` in
		// `origin/main:src/deluge/gui/menu_item/stem_export/start.h`) is the first statement of main's body, and
		// under the guest what is open is `ShellHostUI` rather than `soundEditor`. The dispatcher's answer is
		// `SceneStack::remove(SceneKind::FirstApp)` on the guest's own stack -- the same statement `MenuScene::handle`
		// already uses for the short-Back exit, which IS `exitCompletely()` reached through `SoundEditor::goUpOneLevel`
		// (`MenuScene::handle_event`'s Back arm, `ui/scenes/menu/menu_scene.cpp`; the Back arm of
		// `SoundEditor::buttonAction` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`, the call to
		// `exitCompletely`). The full argument, including why neither `closeUI` nor `closeGuest()` can be called from
		// there, is at `closePortedMenu` in `gui/ui/shell_action_dispatch.cpp`.
		//
		// The guest is ticked through `Shell::tick` and NOT through `timerCallback`, on purpose: it
		// is `timerCallback`'s empty-stack arm that performs
		// the actual `closeUI`, and running that here would put a real root view's `focusRegained`
		// inside the example. What this pins is the dispatcher's half -- the stack -- which is the
		// half the dispatcher owns.
		select_language();
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;

		{
			// Control: one call different, `dispatchShellActions()`. The menu is still up, and the
			// request is really sitting in the outbox.
			GlobalGuestGuard control_guard;
			spec::SongFixture control_song = spec::make_synth_song();
			setRootUILowLevel(&instrumentClipView);
			expect(open_global_guest(nodes::stem_export, control_song)).to_be_true();
			shellHostUI.shell().tick(5);
			(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true);
			(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, false);
			shellHostUI.shell().tick(20);

			expect(shellHostUI.shell().stack().depth()).to_equal(size_t(1));
			expect(scene().take_action().action).to_equal(MenuAction::StartStemExport);
		}

		GlobalGuestGuard guest_guard;
		spec::SongFixture song = spec::make_synth_song();
		// The fall-through root again, so this example is about the close and nothing else.
		setRootUILowLevel(&instrumentClipView);
		expect(chosen_stem_export_type()).to_equal(-1);

		expect(open_global_guest(nodes::stem_export, song)).to_be_true();
		shellHostUI.shell().tick(5);
		expect(shellHostUI.shell().stack().depth()).to_equal(size_t(1));
		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true);
		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, false);
		shellHostUI.shell().tick(20);
		// Still up after the tick that filed the request: the scene does not pop itself
		// (`ui/scenes/menu/nodes/actions.h`), which is why this close is the dispatcher's.
		expect(shellHostUI.shell().stack().depth()).to_equal(size_t(1));

		deluge::gui::ui::dispatchShellActions();

		expect(shellHostUI.shell().stack().depth()).to_equal(size_t(0));
		expect(scene().take_action().engaged()).to_be_false();
	});
});

describe menu_action_three_verbs("gui::ui::dispatchShellActions -- the three one-line verbs", $ {
	// The last three arms. Each is one legacy statement -- or, for the two context
	// menus, one legacy *pair* -- so they share a group rather than getting three of their own.
	//
	// **All three run the real legacy callee, and none of them needs a decision function.** That
	// was not obvious: `StartStemExport` has a separate chooser (`stemExportTypeForRootUI`) because its
	// acting half arms the recorder and enters `UI_MODE_STEM_EXPORT`. None of these three does anything
	// comparable -- `setClipLengthEqualToSampleLength` writes the model and shows a popup, and the
	// two openers move a view mode and push a UI onto `uiNavigationHierarchy`, all of which a spec
	// can seat, observe and put back.
	//
	// Every example is a control leg and a subject that **differ in exactly one call**,
	// `dispatchShellActions()` -- same rig, same fixture, same root, same press, same ticks. The
	// control's job is to say the press really filed, so the subject's effect is attributable to the
	// arm and not to a press that never landed.
	//
	// What each example can and cannot see is stated where it bites: `SessionView::gridModeActive`
	// and `previousGridModeActive` are private (`gui/views/session_view.h`), so the half
	// of `enterMidiLearnMode`/`enterMacrosConfigMode` that moves them is asserted only through the
	// state that *is* reachable -- `currentUIMode` and `currentSong->persistentView.sessionLayout`.
	//
	// @par What these examples do NOT pin, and why that is a fact about legacy rather than a hole
	//
	// **The ORDER of the two statements in each context-menu arm.** Swapping them -- `openUI(...)` first,
	// `setupAndCheckAvailability()` second -- leaves this group green; measured, not assumed. The arms are written in
	// main's order because they are a transcription (`MidiLearn::selectButtonPress` in
	// `origin/main:src/deluge/gui/menu_item/song/midi_learn.h`), and it is worth knowing that no test defends it.
	//
	// It is not merely unobserved, though. **It is unobservable**, and the reason is every effect
	// `openUI` has, not just the one that is easy to name. `openUI` does
	// five things, and each is inert with respect to `setupAndCheckAvailability`:
	//
	//   1. **The push** (`uiNavigationHierarchy[numUIsOpen] = newUI`), which happens FIRST inside `openUI` -- before
	//      any of the four below -- so nothing here observes a half-open stack either way.
	//   2. **`uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC)`** (the call right after the push). A *different* timer
	//      from the only one this arm's other statement arms: `View::midiLearnFlash` sets `TimerName::MIDI_LEARN_FLASH`
	//      (`gui/views/view.cpp`). Neither clears the other.
	//   3. **`PadLEDs::reassessGreyout()`** (the call after that). **This is the one that could have made the
	//      order observable, which is why it belongs in the argument rather than being left out of
	//      it.** It recomputes from `getUIGreyoutColsAndRows`, which walks the
	//      hierarchy top-down and takes the first UI that answers true -- and by then the context
	//      menu is already top, in either order. `MidiLearnMode::getGreyoutColsAndRows`
	//      (`gui/context_menu/midi_learn_mode.cpp`) then answers two **constants** and reads no
	//      state at all, `currentUIMode` included. Had that greyout depended on the MIDI-learn mode,
	//      the two orders would give different pad greyouts and this example would be hiding a real
	//      difference.
	//   4. **`newUI->opened()`**. Neither context menu overrides it, so it is `UI::opened`:
	//      `focusRegained()` -- for a `ContextMenu`, one
	//      `blinkLed(IndicatorLED::BACK)` (`gui/context_menu/context_menu.cpp`) -- and then
	//      `return true`, **unconditionally**. That last word matters: the `!success` branch
	//      (`openUI` in `gui/ui/ui.cpp`) pops the UI and calls `oldUI->focusRegained()`, which WOULD be
	//      order-sensitive, and it is unreachable here for that reason rather than by luck. Nothing
	//      on the path -- `getTitle`/`getOptions` included (`midi_learn_mode.cpp`) -- reads
	//      `currentUIMode`, and the LED is not the `IndicatorLED::LEARN` that `View::startMIDILearn`
	//      blinks (`View::startMIDILearn` in `gui/views/view.cpp`).
	//   5. **`renderUIsForOled()`**, which sets `doesOLEDNeedRendering = true` and nothing
	//      else. No frame is composed inside `openUI`, so there is no transient render
	//      in which the mode could differ.
	//
	// And the reverse direction is closed too, which the list above does not cover on its own:
	// `View::midiLearnFlash` calls `getRootUI()->midiLearnFlash()`, so
	// running `setupAndCheckAvailability()` *after* the open would be observable if the open moved
	// the root. It cannot: `getRootUI()` reads `uiNavigationHierarchy[0]` (`gui/ui/ui.cpp`)
	// and `openUI` writes index `numUIsOpen`, which is >= 1 whenever a root view is open --
	// the precondition `stemExportTypeForRootUI`'s own note in `gui/ui/shell_action_dispatch.cpp`
	// states and these examples establish with `setRootUILowLevel(&sessionView)`.

	it("SetClipLengthToSampleLength: sets the clip's length from the sample, as legacy's view does", _ {
		select_language();
		UiStackGuard ui_guard;
		spec::QuietAudio quiet;

		{
			// The control. One call different.
			deluge::ui::scenes::spec::PopupCapture control_popups;
			ActionLogGuard control_log;
			Rig control;
			const spec::AudioSongFixture f = seat_audio_clip_with_sample();
			setRootUILowLevel(&audioClipView);
			const int32_t seeded = f.clip->loopLength();

			expect(press_child_leaving_request(control, nodes::audio_clip_actions, nodes::set_clip_length))
			    .to_be_true();

			expect(f.clip->loopLength()).to_equal(seeded);
			expect(control_popups.seen()).to_equal(std::vector<std::string>{});
			expect(scene().take_action().action).to_equal(MenuAction::SetClipLengthToSampleLength);
		}

		deluge::ui::scenes::spec::PopupCapture popups;
		ActionLogGuard log_guard;
		Rig r;
		const spec::AudioSongFixture f = seat_audio_clip_with_sample();
		// `audioClipView` as the root: the view the audio-clip menu is opened over, whose scene makes the edit.
		setRootUILowLevel(&audioClipView);
		deluge::ui::scenes::audio_clip::audio_clip_scene().open(deluge::edits::clipAddressFor(*f.clip),
		                                                        &f.song->project);
		deluge::base::notify::Notice discard;
		while (deluge::base::notify::notices().pop(discard)) {}
		popups.clear();

		const int32_t seeded = f.clip->loopLength();
		// What `AudioClipView::setClipLengthEqualToSampleLength` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`)
		// handed `adjustLoopLength`, read from the model rather than hard-coded.
		const int32_t from_sample = f.clip->sampleHolder.getLoopLengthAtSystemSampleRate(true);
		// ...and the two differ, which is what stops the assertion below passing on a no-op. Without
		// the tempo `seat_audio_clip_with_sample` writes, this is the line that goes red.
		expect(from_sample).not_().to_equal(seeded);

		expect(press_child_leaving_request(r, nodes::audio_clip_actions, nodes::set_clip_length)).to_be_true();
		deluge::gui::ui::dispatchShellActions();

		// `SetClipLengthEqualToSampleLength::selectButtonPress`
		// (`origin/main:src/deluge/gui/menu_item/audio_clip/set_clip_length_equal_to_sample_length.h`) ->
		// `AudioClipView::setClipLengthEqualToSampleLength`: the length, `adjustLoopLength`'s bars-and-beats readout
		// and the popup.
		expect(f.clip->loopLength()).to_equal(from_sample);
		// Two bars at 120 BPM: `displayNumberOfBarsAndBeats`' "bars : beats : sub-beats", zero-based.
		expect(popups.seen()).to_equal(std::vector<std::string>{"popup: 2 : 0 : 0", "popup: " + std::string(deluge::l10n::get(String::STRING_FOR_CLIP_LENGTH_ADJUSTED))});
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("SetClipLengthToSampleLength: over an audio clip's automation view, the automation view makes the same edit", _ {
		select_language();
		UiStackGuard ui_guard;
		spec::QuietAudio quiet;
		deluge::ui::scenes::spec::PopupCapture popups;
		ActionLogGuard log_guard;
		Rig r;
		const spec::AudioSongFixture f = seat_audio_clip_with_sample();
		setRootUILowLevel(&automationView);
		const int32_t seeded = f.clip->loopLength();
		const int32_t from_sample = f.clip->sampleHolder.getLoopLengthAtSystemSampleRate(true);
		expect(from_sample).not_().to_equal(seeded);

		expect(press_child_leaving_request(r, nodes::audio_clip_actions, nodes::set_clip_length)).to_be_true();
		deluge::gui::ui::dispatchShellActions();

		expect(f.clip->loopLength()).to_equal(from_sample);
		expect(popups.seen()).to_equal(std::vector<std::string>{"popup: 2 : 0 : 0", "popup: " + std::string(deluge::l10n::get(String::STRING_FOR_CLIP_LENGTH_ADJUSTED))});
	});

	it("OpenSongMidiLearn: arms MIDI learn and opens the context menu, which is both statements", _ {
		select_language();
		UiStackGuard ui_guard;
		MidiLearnGuard learn_guard;

		{
			// The control. One call different.
			Rig control;
			// `sessionView` as the root, because that is the only root the item is relevant under
			// (`song::MidiLearn::isRelevant`, `origin/main:src/deluge/gui/menu_item/song/midi_learn.h`) and because
			// `MidiLearnMode::setupAndCheckAvailability` acts on `sessionView` by name
			// (`gui/context_menu/midi_learn_mode.cpp`).
			setRootUILowLevel(&sessionView);
			// `View::startMIDILearn` only arms from `UI_MODE_NONE` or `UI_MODE_MIDI_LEARN`
			// (`View::startMIDILearn`), so the seed is the mode under which legacy succeeds.
			currentUIMode = UI_MODE_NONE;
			expect(press_child_leaving_request(control, nodes::root_song, nodes::song_midi_learn)).to_be_true();

			expect(static_cast<int>(currentUIMode)).to_equal(static_cast<int>(UI_MODE_NONE));
			expect(isUIOpen(&deluge::gui::context_menu::midiLearnMode)).to_be_false();
			expect(scene().take_action().action).to_equal(MenuAction::OpenSongMidiLearn);
		}

		Rig r;
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_NONE;
		expect(press_child_leaving_request(r, nodes::root_song, nodes::song_midi_learn)).to_be_true();
		deluge::gui::ui::dispatchShellActions();

		// `origin/main:src/deluge/gui/menu_item/song/midi_learn.h` `MidiLearn::selectButtonPress` --
		// `MidiLearnMode::setupAndCheckAvailability`
		// (`gui/context_menu/midi_learn_mode.cpp`) -> `SessionView::enterMidiLearnMode`
		// -> `View::startMIDILearn`, whose one globally visible write is
		// `currentUIMode = UI_MODE_MIDI_LEARN`. It is also exactly what
		// `setupAndCheckAvailability` returns (`midi_learn_mode.cpp`), so this assertion says the
		// discarded bool was `true` here as well.
		expect(static_cast<int>(currentUIMode)).to_equal(static_cast<int>(UI_MODE_MIDI_LEARN));
		// `origin/main:src/deluge/gui/menu_item/song/midi_learn.h` `MidiLearn::selectButtonPress` -- the `openUI` that
		// runs whatever the bool said.
		expect(isUIOpen(&deluge::gui::context_menu::midiLearnMode)).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("OpenSongMacros: enters the macros grid mode and opens the context menu", _ {
		select_language();
		UiStackGuard ui_guard;

		{
			// The control. One call different.
			Rig control;
			setRootUILowLevel(&sessionView);
			control.song.song->persistentView.sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
			expect(press_child_leaving_request(control, nodes::root_song, nodes::configure_song_macros)).to_be_true();

			expect(currentSong->persistentView.sessionLayout).to_equal(SessionLayoutType::SessionLayoutTypeRows);
			expect(isUIOpen(&deluge::gui::context_menu::configureSongMacros)).to_be_false();
			expect(scene().take_action().action).to_equal(MenuAction::OpenSongMacros);
		}

		Rig r;
		setRootUILowLevel(&sessionView);
		// Seeded at the layout the effect is NOT, so the assertion below cannot pass on a value that
		// was already there. `Rig` owns this song and nothing outside the example reads it, so there
		// is no guard to put it back.
		r.song.song->persistentView.sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
		expect(press_child_leaving_request(r, nodes::root_song, nodes::configure_song_macros)).to_be_true();
		deluge::gui::ui::dispatchShellActions();

		// `ConfigureMacros::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/song/configure_macros.h`) --
		// `ConfigureSongMacros::setupAndCheckAvailability` (`gui/context_menu/configure_song_macros.cpp`) ->
		// `SessionView::enterMacrosConfigMode`, whose one write to state this spec can read is
		// `currentSong->persistentView.sessionLayout = ...Grid`. Its other two
		// writes, `previousLayout` and `gridModeActive`, are private members of `SessionView`.
		expect(currentSong->persistentView.sessionLayout).to_equal(SessionLayoutType::SessionLayoutTypeGrid);
		// `ConfigureMacros::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/song/configure_macros.h`).
		expect(isUIOpen(&deluge::gui::context_menu::configureSongMacros)).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("an arm that opens a UI leaves the guest's timer unarmed, because it is no longer the guest's", _ {
		// **A hazard only this example can see.** Three arms end in `openUI(...)`, so when they return `getCurrentUI()`
		// is no longer `ShellHostUI` -- and main's `NO_NAVIGATION` semantics correctly leave the menu on the shell
		// stack, so `timerCallback`'s `depth() == 0` close does not fire and `rearmTimer` runs.
		// `TimerName::UI_SPECIFIC` is dispatched to `getCurrentUI()->timerCallback()` (`UITimerManager::routine` in
		// `gui/ui_timer_manager.cpp`), so re-arming there hands this guest's tick to the UI that was just opened over
		// it. For `performanceView` that is not inert: `PerformanceView::timerCallback` forwards into
		// `sessionView`/`arrangerView` (`gui/views/performance_view.cpp`).
		//
		// The guard is in `ShellHostUI::rearmTimer`, and this
		// example is what reddens without it -- measured, not assumed.
		select_language();
		PerformViewGuard perform_guard;
		UiStackGuard ui_guard;
		UiSpecificTimerGuard timer_guard;

		{
			// **The control, and it is the half that makes the subject mean anything.** Identical
			// rig, identical guest, identical settle tick -- with no press, so the drain finds
			// nothing, opens nothing, and the guest is still current when `rearmTimer` runs. The
			// timer IS armed here. Without this leg the subject's `to_be_false()` would also pass on
			// a `rearmTimer` that never arms in this rig at all -- e.g. if `Shell::next_wake()`
			// answered `nullopt` and took the early return.
			GlobalGuestGuard control_guard;
			spec::SongFixture control_song = spec::make_synth_song();
			setRootUILowLevel(&sessionView);
			expect(open_global_guest_on_ui_stack(nodes::perform_editor, control_song)).to_be_true();
			expect(getCurrentUI()).to_equal(&shellHostUI);

			// Cleared right before the tick, so the only thing that can set it is `rearmTimer`:
			// `opened()` and every input hook arm it too, through `armTickSoon`
			// (and the six input hooks).
			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
			(void)shellHostUI.timerCallback();

			expect(getCurrentUI()).to_equal(&shellHostUI); // nothing was opened over it
			expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_true();
			(void)scene().take_action();
		}

		GlobalGuestGuard guest_guard;
		spec::SongFixture song = spec::make_synth_song();
		setRootUILowLevel(&sessionView);
		// VALUE rather than DISABLED: `PerformEditorItem::on_focus` seeds the cursor from the view's mode
		// (`ui/scenes/menu/nodes/actions.cpp`), and DISABLED is the arm that files nothing.
		set_view_mode(PerformanceEditingMode::VALUE);
		expect(open_global_guest_on_ui_stack(nodes::perform_editor, song)).to_be_true();

		(void)shellHostUI.timerCallback();                 // settle the first frame...
		expect(scene().take_action().engaged()).to_be_false(); // ...which files nothing on its own

		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true);

		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, false);
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC); // `buttonAction` armed it via `armTickSoon`
		(void)shellHostUI.timerCallback();

		// The arm really opened the view -- so this is about the re-arm and not about a press that
		// never landed.
		expect(isUIOpen(&performanceView)).to_be_true();
		// ...and the view, not the guest, is who `TimerName::UI_SPECIFIC` would now be delivered to.
		// This is the guard's exact predicate, asserted rather than assumed.
		expect(getCurrentUI()).to_equal(&performanceView);

		expect(uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)).to_be_false();

		// And the outbox is empty, so the effects above came from this one request.
		expect(scene().take_action().engaged()).to_be_false();
	});
});

describe menu_action_perform_fact("UiFacts::performance_editing_mode over the real performanceView", $ {
	// What `PerformEditorItem` reads (`menu/nodes/actions.cpp`), answered by the host from `performanceView`'s two
	// bools (`HostUiFacts::performance_editing_mode`, `gui/ui/shell_host/ui_facts.cpp`). The first example pins the
	// mapping on every pair of the bools. The rest drive the view's own gestures, with no menu and no `Shell`, and
	// check the fact follows each: `PerformanceView::buttonAction`'s BACK arm, its shift+KEYBOARD exit and cycle, and
	// the four callers of the private `PerformanceView::releaseViewOnExit` (`buttonAction`'s CLIP_VIEW arm, its
	// KEYBOARD press and release exits, and `padAction`'s grid-mode pad).
	//
	// Every gesture example asserts the two bools by name and compares `fact_and_view()` against a literal mode, so an
	// answer with VALUE and PARAM swapped reddens.
	//
	// **`getCurrentUI()`.** This group opens real UIs, and `UiStackGuard`'s `nullifyUIs()` leaves the stack empty,
	// where `getCurrentUI()` answers `emptyStackUI`. It reads `isUIOpen`/`getRootUI` rather than `getCurrentUI`
	// wherever it asserts about the stack.

	it("answers each pair of the view's bools as main's readCurrentValue does", _ {
		select_language();
		PerformViewGuard guard;
		// `{false, true}` is unreachable in main; `readCurrentValue` tests `defaultEditingMode` first, so it is
		// DISABLED, and it is asked rather than skipped.
		struct Case {
			bool default_mode;
			bool editing_param;
			PerformanceEditingMode mode;
		};
		const std::array<Case, 4> cases{{{false, false, PerformanceEditingMode::DISABLED},
		                                 {true, false, PerformanceEditingMode::VALUE},
		                                 {true, true, PerformanceEditingMode::PARAM},
		                                 {false, true, PerformanceEditingMode::DISABLED}}};
		for (const Case& c : cases) {
			performanceView.defaultEditingMode = c.default_mode;
			performanceView.editingParam = c.editing_param;
			expect(fact_and_view()).to_equal(both_at(c.mode));
		}
	});

	it("BACK out of editing mode moves the fact to DISABLED", _ {
		// `buttonAction`'s `b == BACK && defaultEditingMode` arm, reached only when
		// `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` is not latched (that arm is tested first).
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/true);
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));

		(void)performanceView.buttonAction(deluge::hid::button::BACK, true);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
	});

	it("shift+KEYBOARD out of param editing moves the fact to DISABLED", _ {
		// The exit half of `buttonAction`'s KEYBOARD arm, guarded by `defaultEditingMode && editingParam`.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/true);
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));

		Buttons::commandToggleShift(true);
		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, true);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
	});

	it("shift+KEYBOARD into editing mode moves the fact to VALUE", _ {
		// The cycle half with `defaultEditingMode` false: `editingParam` is left alone and `defaultEditingMode` is set,
		// which `readCurrentValue` answers VALUE. The half of the cycle that ENTERS.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/false, /*editing_param=*/false);
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));

		Buttons::commandToggleShift(true);
		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, true);

		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_false();
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::VALUE));
	});

	it("shift+KEYBOARD again, already in value editing, moves the fact to PARAM", _ {
		// The same arm the other way: already in editing mode, `editingParam` is set before `defaultEditingMode`, and
		// the pair is PARAM. VALUE and PARAM from one arm is what makes a swapped answer visible: it reddens exactly one
		// of these two examples.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/false);
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::VALUE));

		Buttons::commandToggleShift(true);
		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, true);

		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_true();
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));
	});

	// The four callers of `releaseViewOnExit`. The helper is private, so each example reaches it through the gesture
	// that calls it.

	it("CLIP_VIEW out of editing mode moves the fact, through releaseViewOnExit", _ {
		// `lastClipInstanceEnteredStartPos` is set non-negative so the CLIP_VIEW arm skips
		// `sessionView.transitionToViewForClip()` -- the arm's own guard, not a stub: main takes that branch whenever
		// performance view was entered from the arranger.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/true);
		currentSong->persistentView.lastClipInstanceEnteredStartPos = 0;
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));

		(void)performanceView.buttonAction(deluge::hid::button::CLIP_VIEW, true);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
	});

	it("KEYBOARD without shift moves the fact, through releaseViewOnExit", _ {
		// The `else` of the KEYBOARD arm's `Buttons::isShiftButtonPressed()`. `lastClipInstanceEnteredStartPos == -1`
		// picks `changeRootUI(&sessionView)` of its three exits, the one it takes from a session-view entry.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/true);
		currentSong->persistentView.lastClipInstanceEnteredStartPos = -1;
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));

		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, true);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
		// ...and it really was the exit branch, not the cycle branch.
		expect(getRootUI()).to_equal(&sessionView);
	});

	it("releasing KEYBOARD after the hold time moves the fact, through releaseViewOnExit", _ {
		// The "performance view was only peeked" release, gated on `timeKeyboardShortcutPress != 0` and on the elapsed
		// time clearing `FlashStorage::holdTime`. Both are set here rather than waited for.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/false);
		currentSong->persistentView.lastClipInstanceEnteredStartPos = -1;
		performanceView.timeKeyboardShortcutPress = 1;
		AudioEngine::audioSampleTimer = 1 + static_cast<uint32_t>(FlashStorage::holdTime);
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::VALUE));

		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, false);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
		expect(getRootUI()).to_equal(&sessionView);
	});

	it("a grid mode pad leaves the fact at DISABLED, through releaseViewOnExit", _ {
		// `padAction`'s grid-mode arm is guarded by `!defaultEditingMode`, so the view is already DISABLED when the
		// helper runs; the exit must leave it there.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/false, /*editing_param=*/false);
		currentSong->persistentView.lastClipInstanceEnteredStartPos = -1; // the session-view half
		currentSong->persistentView.sessionLayout = SessionLayoutType::SessionLayoutTypeGrid; // the grid half
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));

		// `kDisplayWidth + 1` is the second sidebar column, `GridMode::GREEN` one of its two mode pads.
		(void)performanceView.padAction(kDisplayWidth + 1, GridMode::GREEN, 1);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(fact_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
		expect(getRootUI()).to_equal(&sessionView);
	});
});

describe menu_action_settings("settings writes that legacy applies at once", $ {
	// `PadBrightness`, `ShiftIsSticky`, `flash::Status`, `Colour`, the highlighting item and the rounded-corners toggle
	// each do one thing besides storing, `hid/` or the guest's shell. Each writes a DEVICE row, whose store in the
	// device port does it at the write, as main's `writeCurrentValue` does.
	it("each write applies its side effect at the write, and only on main's own condition", _ {
		menu::MenuContext on_device{};
		on_device.ports = deluge::gui::shell_host::host_ports();
		const uint8_t saved_brightness = FlashStorage::defaultPadBrightness;
		const MenuHighlighting saved_highlighting = FlashStorage::accessibilityMenuHighlighting;
		const uint8_t saved_flash = PadLEDs::flashCursor;
		const uint8_t saved_colour = FlashStorage::padColours[0];

		// `PadBrightness::writeCurrentValue`: the pads' dimmer takes the stored level at once, a quarter of what the
		// item shows.
		PadLEDs::dimmerInterval = 0;
		menu::bindings::write(nodes::default_pad_brightness, on_device, 40);
		expect(static_cast<int>(FlashStorage::defaultPadBrightness)).to_equal(40 >> 2);
		expect(PadLEDs::dimmerInterval).to_equal(int32_t{kMaxLedBrightness - (40 >> 2)});

		// The highlighting item: main's canvas flag and the guest's panels follow the stored value.
		FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::NO_INVERSION;
		deluge::gui::ui::pushShellHighlighting();
		menu::bindings::write(nodes::default_menu_highlighting, on_device,
		                      static_cast<int32_t>(MenuHighlighting::FULL_INVERSION));
		expect(FlashStorage::accessibilityMenuHighlighting).to_equal(MenuHighlighting::FULL_INVERSION);
		expect(deluge::hid::display::oled_canvas::Canvas::menuHighlightingInverts).to_be_true();
		expect(shellHostUI.shell().surfaces().oled()[0].style->highlighting)
		    .to_equal(deluge::ui::widgets::core::Highlighting::FullInversion);

		// `flash::Status::writeCurrentValue`, through the play cursor's device row: leaving the slow flash clears its
		// tick squares and, the slow flash still current, sends the column pair that held one; leaving another does
		// nothing. The send shows in the harness's pad shadow (column 2, row 2 below), or waits for room in the PIC's
		// queue. A clear made after the store sends nothing.
		const auto sent = [] {
			const uint8_t* p = host_script_pad_shadow() + (2 * 8 + 2) * 3;
			return (int32_t{p[0]} << 16) | (int32_t{p[1]} << 8) | int32_t{p[2]};
		};
		const RGB saved_pad = PadLEDs::image[2][2];
		currentUIMode = UI_MODE_NONE;
		PadLEDs::needToSendOutMainPadColours = false;
		PadLEDs::flashCursor = FLASH_CURSOR_SLOW;
		PadLEDs::slowFlashSquares[2] = 3; // row 2's square on column 3: columns 2 and 3 go out as one pair
		const int32_t before = sent();
		// A colour the shadow does not hold, so a send of column 2 changes it.
		PadLEDs::image[2][2] = RGB{.r = static_cast<uint8_t>((before >> 16) ^ 0x55),
		                            .g = static_cast<uint8_t>((before >> 8) ^ 0x55),
		                            .b = static_cast<uint8_t>(before ^ 0x55)};
		menu::bindings::write(nodes::flash_status, on_device, FLASH_CURSOR_FAST);
		expect(static_cast<int32_t>(PadLEDs::slowFlashSquares[2])).to_equal(255);
		const bool went_out = sent() != before || PadLEDs::needToSendOutMainPadColours;
		expect(went_out).to_be_true();
		PadLEDs::image[2][2] = saved_pad;
		PadLEDs::needToSendOutMainPadColours = false;
		PadLEDs::slowFlashSquares[2] = 3;
		menu::bindings::write(nodes::flash_status, on_device, FLASH_CURSOR_OFF);
		expect(static_cast<int32_t>(PadLEDs::slowFlashSquares[2])).to_equal(3);

		// `Colour::writeCurrentValue`: every pad re-renders, whatever the UI.
		whichMainRowsNeedRendering = 0;
		menu::bindings::write(nodes::pad_colour_active, on_device, 3);
		expect(static_cast<int>(FlashStorage::padColours[0])).to_equal(3);
		expect(whichMainRowsNeedRendering).to_equal(0xFFFFFFFFu);

		// Nothing waits in the menu for a dispatcher.
		expect(scene().state().walk.pending_action.engaged()).to_be_false();

		FlashStorage::padColours[0] = saved_colour;
		PadLEDs::slowFlashSquares[2] = 255;
		PadLEDs::flashCursor = saved_flash;
		FlashStorage::accessibilityMenuHighlighting = saved_highlighting;
		deluge::gui::ui::pushShellHighlighting();
		FlashStorage::defaultPadBrightness = saved_brightness;
		PadLEDs::setBrightnessLevel(saved_brightness);
	});

	it("closing the guest on the settings menu saves the settings, as SoundEditor::exitCompletely does", _ {
		// `SoundEditor::exitCompletely`'s settings arm (`origin/main:src/deluge/gui/ui/sound_editor.cpp`):
		// `FlashStorage::writeSettings` and the three settings files, on leaving the settings menu and only then.
		deluge::ui::scenes::spec::ErasedFlash flash;
		deluge::ui::scenes::spec::GlobalGuestGuard guest_guard;
		const MenuHighlighting was = FlashStorage::accessibilityMenuHighlighting;
		deluge::ui::scenes::spec::SongFixture fx = deluge::ui::scenes::spec::make_synth_song();

		expect(deluge::ui::scenes::spec::open_menu_on_global_guest_at(fx, menu::MenuContext::Root::Song)).to_be_true();
		FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::NO_INVERSION;
		deluge::gui::ui::saveSettingsBeforeClose();
		FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::FULL_INVERSION;
		FlashStorage::readSettings(); // erased flash: the defaults, not what was set
		expect(FlashStorage::accessibilityMenuHighlighting).not_().to_equal(MenuHighlighting::NO_INVERSION);

		deluge::ui::scenes::spec::GlobalGuestGuard::empty_guest_stack();
		expect(deluge::ui::scenes::spec::open_menu_on_global_guest_at(fx, menu::MenuContext::Root::Settings)).to_be_true();
		FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::NO_INVERSION;
		deluge::gui::ui::saveSettingsBeforeClose();
		FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::FULL_INVERSION;
		FlashStorage::readSettings();
		expect(FlashStorage::accessibilityMenuHighlighting).to_equal(MenuHighlighting::NO_INVERSION);
		FlashStorage::accessibilityMenuHighlighting = was;
	});

	it("pushing the card's highlighting restyles the guest's panels", _ {
		const MenuHighlighting was = FlashStorage::accessibilityMenuHighlighting;
		FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::NO_INVERSION;
		deluge::gui::ui::pushShellHighlighting();
		expect(shellHostUI.shell().surfaces().oled()[0].style->highlighting)
		    .to_equal(deluge::ui::widgets::core::Highlighting::NoInversion);
		FlashStorage::accessibilityMenuHighlighting = was;
		deluge::gui::ui::pushShellHighlighting();
	});

	it("a rounded-corners write reaches main's canvas and the guest's panels at once", _ {
		// `RoundedCornersSettingToggle::writeCurrentValue` sets `Canvas::roundedCornersEnabled`, which main's rounded
		// primitives read; the guest's panels draw from their own style. Both move at the write.
		select_language();
		const uint32_t saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners);
		Rig r;
		(void)r.open(nodes::root_settings);
		r.shell.tick(5);
		menu::MenuContext ctx{};
		ctx.ports = deluge::gui::shell_host::host_ports(); // a DEVICE row is written through the device port
		menu::bindings::write(nodes::feature_rounded_corners, ctx, 0);
		expect(deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled).to_be_false();
		expect(shellHostUI.shell().surfaces().oled()[0].style->rounded).to_be_false();
		menu::bindings::write(nodes::feature_rounded_corners, ctx, 1);
		expect(deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled).to_be_true();
		expect(shellHostUI.shell().surfaces().oled()[0].style->rounded).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::RoundedCorners, saved);
		deluge::gui::ui::pushShellRoundedCorners();
	});

	it("pushing the card's rounded corners restyles both, as the settings read does", _ {
		const uint32_t saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::RoundedCorners, RuntimeFeatureStateToggle::Off);
		deluge::gui::ui::pushShellRoundedCorners();
		expect(deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled).to_be_false();
		expect(shellHostUI.shell().surfaces().oled()[0].style->rounded).to_be_false();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::RoundedCorners, saved);
		deluge::gui::ui::pushShellRoundedCorners();
	});

	it("a horizontal-menus write reaches the open menu, so the voice usage page lays out as it now says", _ {
		// `HorizontalMenu::renderingStyle()` (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`) reads the
		// setting on every draw; the port's context holds a copy.
		select_language();
		const uint32_t saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::HorizontalMenus);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		Rig r;
		(void)r.open(nodes::root_settings);
		r.shell.tick(5);
		expect(menu::is_horizontal(scene().menu_context(), nodes::voice_usage)).to_be_true();
		menu::bindings::write(nodes::feature_horizontal_menus, scene().menu_context(), 0);
		expect(runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus)).to_be_false();
		expect(menu::is_horizontal(scene().menu_context(), nodes::voice_usage)).to_be_false();
		menu::bindings::write(nodes::feature_horizontal_menus, scene().menu_context(), 1);
		expect(menu::is_horizontal(scene().menu_context(), nodes::voice_usage)).to_be_true();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, saved);
	});

	it("a sticky-shift write reaches the guest's own shift machine, and turning it off releases both latches", _ {
		// `ShiftIsSticky::writeCurrentValue`: `shellHostUI.shell().lowering().set_shift_is_sticky(on)`, and off,
		// `Buttons::clearShiftSticky()`, both at the write.
		select_language();
		const uint32_t saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky);
		const uint32_t saved_light = runtimeFeatureSettings.get(RuntimeFeatureSettingType::LightShiftLed);
		Rig r;
		(void)r.open(nodes::root_settings);
		r.shell.tick(5);
		menu::MenuContext ctx{};
		ctx.ports = deluge::gui::shell_host::host_ports(); // a DEVICE row is written through the device port
		shellHostUI.shell().lowering().set_shift_is_sticky(false);

		menu::bindings::write(nodes::feature_sticky_shift, ctx, 1);
		expect(shellHostUI.shell().lowering().shift_is_sticky()).to_be_true();

		Buttons::commandToggleShift(true); // SHIFT held: legacy's own state, which the off arm releases
		menu::bindings::write(nodes::feature_sticky_shift, ctx, 0);
		expect(shellHostUI.shell().lowering().shift_is_sticky()).to_be_false();
		expect(Buttons::isShiftButtonPressed()).to_be_false();

		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, saved);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, saved_light);
		shellHostUI.shell().lowering().set_shift_is_sticky(saved == RuntimeFeatureStateToggle::On);
		Buttons::clearShiftSticky();
	});

	it("shows a pad-grid layout the menu does not list as its last choice, as main's list clamps it", _ {
		// The flash may hold any of `KeyboardLayoutType`'s seven layouts; `defaults::KeyboardLayout::getOptions`
		// (`origin/main:src/deluge/gui/menu_item/defaults/keyboard_layout.h`) lists the first three.
		select_language();
		const KeyboardLayoutType saved = FlashStorage::defaultKeyboardLayout;
		FlashStorage::defaultKeyboardLayout = KeyboardLayoutType::KeyboardLayoutTypeChord;
		{
			Rig r;
			const MenuContext ctx = r.open(nodes::default_ui_keyboard);
			r.shell.tick(5);
			const int32_t row = visible_index_of(nodes::default_ui_keyboard, nodes::default_keyboard_layout, ctx);
			expect(row).to_equal(0);
			r.enter(20);
			r.shell.tick(30);
			expect(scene().focused()).to_equal(&nodes::default_keyboard_layout);
			expect(static_cast<int32_t>(scene().state().enum_index)).to_equal(2);
			std::array<std::string_view, menu::kMaxDynamicNames> names{};
			std::array<deluge::ui::shell::TextScratch, menu::kMaxDynamicNames> scratch{};
			const size_t count = menu::bindings::names(nodes::default_keyboard_layout, ctx, names, scratch);
			expect(count).to_equal(size_t{3});
			if (count == 3) {
				expect(std::string{names[2]}).to_equal(std::string{"Piano"});
			}
			r.shell.tick(60);
		}
		// Showing it heals nothing: the flash keeps the layout the menu does not list.
		expect(static_cast<int>(FlashStorage::defaultKeyboardLayout))
		    .to_equal(static_cast<int>(KeyboardLayoutType::KeyboardLayoutTypeChord));
		FlashStorage::defaultKeyboardLayout = saved;
	});
});

describe menu_action_reference_rows("scenes::menu actions: the reference rows", $ {
	it("parses menu_actions.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{4});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(menu_actions_spec, menu_action_settings, menu_action_seam, menu_action_items, menu_action_perform,
            menu_action_dispatch, menu_action_screensaver, menu_action_open_perform, menu_action_stem_export,
            menu_action_three_verbs, menu_action_perform_fact, menu_action_reference_rows)
