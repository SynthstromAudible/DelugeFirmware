/// @file
/// @brief The **action seam** (`scenes/menu/action.h`) and the five items that use it, driven
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
///   * **Covered as of F2 task 1, in `menu_action_dispatch`:** that `gui::ui::dispatchShellActions`
///     is what takes a filed request out of the outbox. That is the consumer half's *seam*, not its
///     body.
///   * **Covered as of F2 task 2, in `menu_action_open_perform`:** that `OpenPerformanceView` is
///     *performed* -- the two `performanceView` bools, the conditional `resetPerformanceView`, and
///     the `openUI`.
///   * **Covered as of F2 task 3, in `menu_action_stem_export`:** the whole of `StartStemExport`'s
///     `getRootUI()` fan-out, driven by name through `stemExportTypeForRootUI()`, plus the arm end
///     to end down the one branch that starts nothing, plus the close.
///   * **Covered as of F2 task 4, in `menu_action_three_verbs`:** that the remaining three verbs are
///     *performed* -- the clip's length really moves to the sample's, MIDI learn really arms, and
///     each context menu really lands on `uiNavigationHierarchy`.
///   * **NOT covered:** the acting half of `StartStemExport` -- `startStemExportProcess` would run a
///     real export inline on this BSP (see that group's own note) -- and
///     `audio_clip_view.cpp:766-768`'s `STRING_FOR_NO_SAMPLE` arm, which no `AudioClip` can reach
///     (see `menu_action_three_verbs`' first example).
///
/// Legacy is the oracle wherever it can be reached: `performEditorMenu`'s real option list and its
/// real `readCurrentValue` mapping are both run here, on the real `performanceView`, and compared
/// against the port; and as of task 4 the three one-line verbs run their real legacy callees on the
/// real `audioClipView`, `sessionView` and `gui/context_menu/` singletons. The identity pinning that
/// preceded those bodies is kept -- it is what says the right *item* files the right *action*, which
/// no effect assertion can say on its own.

#include "scenes/l10n_bridge.h"
#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/actions.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/shortcuts.h" // menu::shortcuts_version -- the port half of the setting
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

// `card_access.h` before CppSpec, for the reason its own `@warning` gives.
#include "support/card_access.h"

#include "gui/context_menu/configure_song_macros.h"
#include "gui/context_menu/midi_learn_mode.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/performance_session_view/editing_mode.h"
#include "gui/ui/menus.h"   // paramShortcutsForSounds and friends -- ShortcutsVersionGuard's cells
#include "gui/ui/root_ui.h" // PerformExitStubUI's base, for the arms that close()
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/sound_editor.h" // soundEditor.shortcutsVersion, SHORTCUTS_VERSION_1 / _3
#include "gui/ui/ui.h"           // isUIOpen / nullifyUIs / setRootUILowLevel, for the groups that use the real UI stack
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h" // commandToggleShift / clearShiftSticky, for the shift+KEYBOARD gestures
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h" // audioSampleTimer, the KEYBOARD release arm's clock
#include "processing/stem_export/stem_export.h"
#include "storage/flash_storage.h" // holdTime, the other half of that arm's gate

#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"

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

#include "spec/support/spec_main.h"

/// The real `performance_session_view::EditingMode` singleton. Declared bare at global scope
/// because `gui/ui/menus.cpp:2005` defines it there (that file's `using namespace` lines are what
/// make its `menu_item::` spelling work) and `gui/ui/menus.h` does not export it -- the same
/// situation `tables_3_spec.cpp:589` records for `flashStatusMenu` and friends.
extern deluge::gui::menu_item::performance_session_view::EditingMode performEditorMenu;

// `modSourceShortcuts` is a file-scope global in `sound_editor.cpp:66` and named in no header;
// `ShortcutsVersionGuard` below has to restore the two cells the V1 arm writes. Declared exactly as
// `shortcuts_spec.cpp:73` declares it, for the same reason.
extern "C++" PatchSource modSourceShortcuts[2][8];

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuAction;
using deluge::scenes::menu::MenuActionRequest;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// @brief Per-example reset, exactly `menu_scene_spec.cpp`'s: pick a language (nothing boots here,
///        so `l10n::chosenLanguage` would otherwise be null) and drain the process-global notice
///        queue so one example's notification cannot put an overlay on the next one's stack.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

/// Board encoder index (`kDelugeEncoders`, `shell/profiles/deluge.cpp`): 3 = Select.
constexpr uint8_t kSelectEncoder = 3;
/// Matrix coordinates of the select-encoder button (`kMatrix`, `shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

/// @brief The real roots this spec navigates, so `assign_node_ids` numbers everything it touches.
///
/// `root_settings` joined for F2 task 6: `menu_action_shortcuts_version` opens `pads_settings` as a
/// root, and `Rig::open` needs `menu::node_id` to have numbered it. Adding a root here is safe for
/// every other group -- ids are looked up per node, never assumed -- and it is the whole settings
/// subtree that gets numbered, which is what `nodes::shortcuts_version` needs.
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

	/// @brief Tear the stack down, so every scene's `on_exit` always runs (E3 task 10).
	///
	/// `MenuScene` is a process singleton (`shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `recorder_scene_spec.cpp`'s `~Rig` was written to stop (a leaked recorder there made the next
	/// `on_enter` refuse outright and killed 13 of 16 examples on one mutant's first run). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	MenuContext open(const Node& root) {
		// The song root is opened from a root UI, not over a clip -- and since E3 task 9 that matters:
		// `song_midi_learn` and `configure_song_macros` carry `relevance::is_session_view`, so a clip
		// context would list neither of the two rows this spec presses. `sound_editor.cpp:1802-1805`
		// is the arm being stood in for; `sessionView` of its three views, because that is the one
		// where both items are shown.
		const bool song_root = &root == &nodes::root_song;
		const MenuContext ctx = menu::context_for(*song.song, song_root ? nullptr : song.clip, 0, false,
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
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	void enter(uint32_t at_ms) { button(kSelectEncButtonX, kSelectEncButtonY, true, at_ms); }
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
///       (`gui/ui/shell_guest_services.cpp:66`, `:173`). That is documented as idempotent, so
///       re-running it is a re-seed and not a second set; and it leaves this guest running on
///       `Services{}`, which is what `Rig` above runs on too. Nothing here reads a service.
struct GuestRig {
	spec::SongFixture song = spec::make_synth_song();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	deluge::gui::ui::ShellHostUI ui{reg, deluge::ui::shell::kDelugeBoard, deluge_profile()};

	GuestRig(const GuestRig&) = delete;
	GuestRig& operator=(const GuestRig&) = delete;
	GuestRig() = default;

	/// Tear the stack down, so every scene's `on_exit` runs and the next example gets a fresh
	/// `MenuScene` -- `Rig::~Rig`'s reason, verbatim, and the same singleton.
	~GuestRig() {
		while (ui.shell().stack().depth() > 0) {
			ui.shell().stack().pop(SceneKind::Null);
		}
	}

	/// `Rig::open`'s two halves, through the guest's own door: point the scene at @p root with a
	/// session-view context, then let `opened()` do the `Shell::init` push.
	bool open(const Node& root) {
		const bool song_root = &root == &nodes::root_song;
		const MenuContext ctx = menu::context_for(*song.song, song_root ? nullptr : song.clip, 0, false,
		                                          /*session_or_arranger_root=*/song_root,
		                                          /*session_view_root=*/song_root, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		ui.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ui.opened();
	}

	/// The select-encoder press, through the guest's own hook rather than the shell's -- which is
	/// the point: `buttonAction` only queues, and `Shell::pump_input` inside a tick is what routes
	/// (`shell/shell.cpp:579-589`).
	void press_enter() { (void)ui.buttonAction(deluge::hid::button::SELECT_ENC, true, false); }

	/// One detent of the select encoder, through the guest's own hook. `ShellHostUI::selectEncoderAction`
	/// (`gui/ui/shell_host_ui.cpp:807`) is the override legacy's `interpretEncoders` calls
	/// (`hid/encoder_input.cpp:112`, `:117`), and it queues into the same lowering `press_enter`
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
		if (child->relevant == nullptr || child->relevant(ctx)) {
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

/// @brief The port's transcription of `EditingMode::readCurrentValue` (`editing_mode.h:35-46`),
///        which is the mapping the wiring plan owes a call to `set_performance_editing_mode` for.
///
/// Written here rather than in the library on purpose: the library holds a *mirror*, and this is the
/// rule that must keep it in step. The example below runs it against the real legacy item on the
/// real `performanceView`, which is the only place the two can be compared at all.
PerformanceEditingMode mode_from_view() {
	if (!performanceView.defaultEditingMode) {
		return PerformanceEditingMode::DISABLED;
	}
	if (!performanceView.editingParam) {
		return PerformanceEditingMode::VALUE;
	}
	return PerformanceEditingMode::PARAM;
}

/// @brief Save and restore the two `performanceView` bools around an example that moves them.
struct PerformViewGuard {
	bool saved_default = performanceView.defaultEditingMode;
	bool saved_param = performanceView.editingParam;
	PerformanceEditingMode saved_mirror = menu::performance_editing_mode();
	PerformViewGuard(const PerformViewGuard&) = delete;
	PerformViewGuard& operator=(const PerformViewGuard&) = delete;
	PerformViewGuard() = default;
	~PerformViewGuard() {
		performanceView.defaultEditingMode = saved_default;
		performanceView.editingParam = saved_param;
		menu::set_performance_editing_mode(saved_mirror);
	}
};

/// @brief Put the legacy UI hierarchy back to "nothing open" after an example let the dispatcher
///        `openUI(&performanceView)`.
///
/// `nullifyUIs()` rather than `closeUI`, on `menu_entry_spec.cpp`'s terms: `closeUI` indexes
/// `uiNavigationHierarchy[-1]` when the view it closes is the only entry (`gui/ui/ui.cpp:183-194`),
/// and `nullifyUIs()` is what legacy itself calls to tear the hierarchy down (`:257-261`).
///
/// **The one residue this cannot put back, stated rather than left to be discovered.**
/// `nullifyUIs()` also sets `lastUIBeforeNullifying = getCurrentUI()` (`gui/ui/ui.cpp:258`), and
/// that variable is defined at `gui/ui/ui.cpp:66` with no declaration in `ui.h`, so it cannot be
/// reset from here -- `tables_3_spec.cpp:1334-1343` records the same fact for the same reason. So
/// after an example in this group, `getCurrentUI()` (`ui.cpp:136-140`) answers `&performanceView`
/// instead of null, while `getRootUI()` still answers null -- it is a different function, at
/// `ui.cpp:145-150`, and it reads only `numUIsOpen` (`:146-147`), never the residue. It is
/// benign for this file on two counts, both checkable: nothing in `sim/scenes_spec/menu_actions_spec.cpp`
/// reads `getCurrentUI()`, and `menu_action_open_perform` is registered LAST in `add_specs` below,
/// after the three groups that drive the port. An example added to this file that needs a null
/// `getCurrentUI()` must say so itself.
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
	// The reset marker. `resetPerformanceView` opens with `resetPadPressInfo()`
	// (`gui/views/performance_view.cpp:1364-1365`), which is `initPadPress(firstPadPress)` +
	// `initPadPress(lastPadPress)` (`:1415-1418`), and `initPadPress` clears `isActive` (`:272-278`).
	// `lastPadPress` is the half of that pair the header makes public (`performance_view.h:119`).
	//
	// Nothing else on the dispatcher's path writes it: the only other writers in the whole view are
	// `padPressAction` (`:1182`, the writes at `:1199-1203`), `padReleaseAction`'s `initPadPress`
	// (`:1207`, the call at `:1229`) and `restorePreviousHoldPress` (`:1317-1320`), none of which
	// `openUI` -> `opened()` -> `focusRegained()` (`:328-345`) can reach. So `isActive` going false
	// means `resetPerformanceView` ran, and nothing else does.
	performanceView.lastPadPress.isActive = true;
}

/// @brief Open `perform_editor`, walk the cursor onto @p mode and press select, through the real
///        widget and the real lowering -- `press_child`'s walk, on the one page whose payload varies.
///
/// Leaves the filed request in the outbox **undrained**, deliberately: in this group the dispatcher
/// is the only thing allowed to read it.
void press_perform_mode(Rig& r, PerformanceEditingMode mode) {
	// Seed the cursor at DISABLED so the walk below is `mode` detents from 0 -- the same setup the
	// "files OpenPerformanceView carrying the chosen mode" example uses, and for the same reason:
	// `PerformEditorItem::on_focus` seeds the cursor from the mirror (`nodes/actions.cpp:121`).
	menu::set_performance_editing_mode(PerformanceEditingMode::DISABLED);
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
/// `exportMixdown` (`processing/stem_export/stem_export.h:66`) is what legacy's arranger arm
/// branches on (`gui/menu_item/stem_export/start.h:35`) and is a user setting elsewhere, so an
/// example that moves it must put it back. `processStarted` (`:53`) and `currentStemExportType`
/// (`:55`) are the first two things `StemExport::runStemExportProcess` writes
/// (`processing/stem_export/stem_export.cpp:102-103`), and are how the group below says that no
/// export was started.
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

/// @brief Open the ported menu at @p root on the ONE guest whose stack the dispatcher's close acts
///        on, over @p song's context -- `GuestRig::open`, aimed at the global instead of a local.
///
/// **This is the only thing in this file that touches `shellHostUI`, and it has to be.**
/// `closePortedMenu` removes the menu scene from `shellHostUI.shell().stack()` (see
/// `gui/ui/shell_action_dispatch.cpp`), because the guest is a singleton with one open site
/// (`gui/ui/shell_menu_entry.cpp:162`) and this seam's sibling reads the same global for the same
/// kind of question (`menuIsFrontmost`, `gui/ui/shell_guest_services.cpp:190-192`). A local
/// `GuestRig` has its own `Shell`, so its stack is not the one production empties -- an example
/// built on one could only ever assert that nothing happened to it.
///
/// The contamination `GuestRig`'s note warns about is the scene left behind, and `GlobalGuestGuard`
/// is what answers it: every example using this leaves the global at depth 0, the state it was
/// constructed in. `menu_entry_spec.cpp` drives the same global harder still, through
/// `openUI(&shellHostUI)` (`:147`).
///
/// No `openUI` here, deliberately: this example needs the menu on the guest's *scene* stack, not the
/// guest on `uiNavigationHierarchy`. Keeping it off means no `closeUI(&shellHostUI)` can run in this
/// process, and so no root view's `focusRegained` either.
bool open_global_guest(const Node& root, spec::SongFixture& song) {
	const bool song_root = &root == &nodes::root_song;
	const MenuContext ctx = menu::context_for(*song.song, song_root ? nullptr : song.clip, 0, false,
	                                          /*session_or_arranger_root=*/song_root,
	                                          /*session_view_root=*/song_root, /*kit_global_fx=*/false);
	(void)menu::assign_node_ids(kRoots);
	menu_scene().open(root, ctx);
	shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(root)});
	return shellHostUI.opened();
}

/// @brief What `stemExportTypeForRootUI()` chose, as an `int`, with `-1` for "nothing".
///
/// One expectation per answer, and never `operator*` on the optional: `expect` does not abort the
/// example, so a two-step `has_value()` then `*chosen` turns a red assertion into a libstdc++
/// `_M_is_engaged()` abort that takes the rest of the group with it -- measured, on the first
/// mutation run of this group. `-1` is outside `StemExportType`'s four enumerators
/// (`definitions_cxx.hpp:247-252`), so "nothing" compares unequal to every real answer rather than
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

// --- F2 task 4: the three one-line verbs --------------------------------------------------------

/// @brief The `Display` the `SetClipLengthToSampleLength` group needs, recording what it was asked
///        to show.
///
/// `display` is null in a spec process (`hid/display/display.cpp`), and
/// `AudioClipView::setClipLengthEqualToSampleLength` dereferences it on **both** of its branches
/// (`gui/views/audio_clip_view.cpp:764`, `:767`) -- so an example that drives that callee with no
/// `Display` installed segfaults rather than failing. `audio_clip_scene_spec.cpp:227-242`'s
/// `QuietDisplay` is the same object with the popups thrown away; this one keeps them, because the
/// popup is one of the two legacy effects this group asserts.
struct RecordingDisplay final : deluge::hid::Display {
	std::vector<std::string> popups;

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override {
		popups.emplace_back(text != nullptr ? text : "");
	}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return !popups.empty(); }
	bool hasPopupOfType(PopupType) override { return !popups.empty(); }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief Install a `RecordingDisplay` for the scope and put `display` back to what it was.
struct DisplayGuard {
	deluge::hid::Display* saved = display;
	RecordingDisplay recorder;
	DisplayGuard(const DisplayGuard&) = delete;
	DisplayGuard& operator=(const DisplayGuard&) = delete;
	DisplayGuard() { display = &recorder; }
	~DisplayGuard() { display = saved; }
};

/// @brief One `MISC` action in an otherwise empty undo log, and an empty one again afterwards.
///
/// Not a convenience: `ClipView::lengthenClip` opens with an **unguarded**
/// `actionLogger.firstAction[BEFORE]->type` (`gui/views/clip_view.cpp:98`), as does `shortenClip`
/// (`:129`), so driving a length edit with an empty log segfaults rather than failing.
/// `audio_clip_scene_spec.cpp:396-403` records the same fact and picks the same `ActionType::MISC`,
/// which no test in either function matches.
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
/// `View::endMIDILearn` (`gui/views/view.cpp:499-517`), whose first statement can write the settings
/// file (`:500-503`) and whose fourth calls `getRootUI()->midiLearnFlash()` (`:507-509`) -- a
/// teardown that does more than undo.
///
/// `SessionView::gridModeActive` and `previousGridModeActive` are **private** (`session_view.h:231`,
/// `:202`), so neither can be saved here nor asserted below; what an example after this one would
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
		uiTimerManager.unsetTimer(TimerName::MIDI_LEARN_FLASH); // armed by `View::midiLearnFlash` (`:815`)
		view.shouldSaveSettingsAfterMidiLearn = saved_should_save;
		view.thingPressedForMidiLearn = saved_pressed;
		view.midiLearnFlashOn = saved_flash_on;
		currentUIMode = saved_ui_mode;
	}
};

/// @brief A `Sample` with a length and nothing else.
///
/// `audio_clip_scene_spec.cpp:175-199`'s `make_sample()` minus everything only the waveform scan
/// reads (the cluster size and the overview cache): `SampleHolder::getLoopLengthAtSystemSampleRate`
/// asks for `getDurationInSamples(/*forTimeStretching=*/true)`, which is `endPos - startPos` and
/// never touches the `Sample` at all (`model/sample/sample_holder.cpp:81-92`). The object still has
/// to exist, because `:106`'s `if (audioFile)` is what picks that branch over "return the clip's
/// current length" (`:112`).
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
/// @warning **Construct this AFTER the `Rig`**, or the `Rig`'s own `make_synth_song()` takes
///          `currentSong` back and `getCurrentAudioClip()` (`model/song/song.cpp:98-104`) answers
///          null inside the callee -- which it dereferences (`gui/views/audio_clip_view.cpp:759-760`).
spec::AudioSongFixture seat_audio_clip_with_sample() {
	spec::AudioSongFixture f = spec::make_audio_clip_song();

	// The sample, attached the way `AudioClip::setupPlaybackBounds` leaves it: the whole file,
	// forwards. `guide.audioFileHolder` needs no assignment -- `AudioClip::AudioClip` points it at
	// `sampleHolder` permanently (`model/clip/audio_clip.cpp:57`).
	f.clip->sampleHolder.audioFile = make_length_only_sample(kSampleFrames);
	f.clip->sampleHolder.startPos = 0;
	f.clip->sampleHolder.endPos = static_cast<int64_t>(kSampleFrames);
	// `SampleHolder::setAudioFile` normally computes this (`model/sample/sample_holder.cpp:110`);
	// the fixture attaches by hand, so it is set by hand. `kMaxSampleValue` is the identity for
	// `getLengthInSamplesAtSystemSampleRate` (`:94-102`) -- and a **SIGFPE** at zero.
	f.clip->sampleHolder.neutralPhaseIncrement = kMaxSampleValue;

	// A realistic tempo, which `Song::setupDefault()` does not leave behind: a fresh song's
	// `timePerTimerTickBig` makes `getTimePerTimerTickFloat()` answer 2^32
	// (`model/song/song.cpp:3213-3215`), and `getLoopLengthAtSystemSampleRate` divides by it
	// (`model/sample/sample_holder.cpp:105-113`) -- so the computed length would be **zero** and
	// `adjustLoopLength` would write nothing while every assertion about "a popup happened" still
	// passed. `audio_clip_scene_spec.cpp:410-422` found that the hard way; this is its arithmetic,
	// 120 BPM through `Song::setBPMInner`'s own formula (`song.cpp:3072-3081`) rather than by
	// calling it, because `setTempoFromNumSamples` reaches a transport this process does not have.
	const double bpm = 120.0 * static_cast<double>(1u << f.song->insideWorldTickMagnitude);
	f.song->timePerTimerTickBig = static_cast<uint64_t>((110250.0 / bpm) * 4294967296.0);
	return f;
}

// --- F2 task 5: the four `PerformanceView` sites that move the mirrored bools --------------------

/// @brief A `RootUI` that claims nothing, seated UNDER `performanceView` so the two `buttonAction`
///        arms that `close()` have somewhere to close *to*.
///
/// `closeUI` walks down from `numUIsOpen - 1` while `u >= 1` and then reads
/// `uiNavigationHierarchy[u - 1]` (`gui/ui/ui.cpp:177-199`, the loop at `:183` and the read at
/// `:194`), so closing the only open UI indexes `[-1]` -- the fact `UiStackGuard`'s note above
/// already records for `closeUI`. Seating this at index 0 with `openUI(&performanceView)` on top
/// makes `close()` an in-bounds close onto an object with no behaviour of its own; `focusRegained`
/// is the only method `closeUI` calls on it (`:196`) and `UI` leaves it empty.
/// `midi_learn_spec.cpp:226-235`'s `StubRootUI` is the same object, built for the same reason.
class PerformExitStubUI final : public RootUI {
public:
	ActionResult buttonAction(deluge::hid::Button, bool, bool) override { return ActionResult::DEALT_WITH; }
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

PerformExitStubUI& perform_exit_stub() {
	static PerformExitStubUI instance;
	return instance;
}

/// @brief Save and restore everything OUTSIDE the two bools that this group's gestures move.
///
/// `PerformViewGuard` above owns the two bools and the mirror; this owns the rest of the state the
/// four legacy sites are reached through. Each field is named with the site that needs it:
///
///   * `currentUIMode` -- the CLIP_VIEW arm tests it (`performance_view.cpp:743`), and `close()`
///     and `changeRootUI` both unset `TimerName::UI_SPECIFIC` on the way past.
///   * `lastClipInstanceEnteredStartPos` -- picks which root a KEYBOARD exit changes to
///     (`:960-971`), and is what lets the CLIP_VIEW arm skip `transitionToViewForClip()` (`:750-752`).
///   * `sessionLayout` -- the grid arm of `padAction` (`:1081`).
///   * `affectEntire` -- `PerformanceView::focusRegained` writes it unconditionally (`:329`), and
///     `openUI` runs that.
///   * `timeKeyboardShortcutPress` -- the KEYBOARD *release* arm's own gate (`:973-977`).
///   * `AudioEngine::audioSampleTimer` -- the other half of that gate's arithmetic (`:976`).
///   * shift -- `Buttons::isShiftButtonPressed()` (`hid/buttons.cpp:294-296`) chooses between the
///     KEYBOARD arm's two halves (`:923`). Pressed with `commandToggleShift(true)` and released with
///     `clearShiftSticky()` (`hid/buttons.cpp:302-306`), which clears `shiftCurrentlyPressed`
///     outright -- `commandToggleShift(false)` would instead run the sticky-shift arithmetic
///     (`:248-269`) and can leave shift *stuck on* for the next example.
struct PerformGestureGuard {
	uint32_t saved_ui_mode = currentUIMode;
	int32_t saved_entered_pos = currentSong->lastClipInstanceEnteredStartPos;
	SessionLayoutType saved_layout = currentSong->sessionLayout;
	bool saved_affect_entire = currentSong->affectEntire;
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
		currentSong->affectEntire = saved_affect_entire;
		currentSong->sessionLayout = saved_layout;
		currentSong->lastClipInstanceEnteredStartPos = saved_entered_pos;
		currentUIMode = saved_ui_mode;
	}
};

/// @brief Seat the stub root, open the real `performanceView` on top of it, put the two bools at
///        @p default_mode / @p editing_param, and park the mirror at @p stale.
///
/// The mirror is seeded to a value the gesture must move it OFF, and every example asserts it is
/// sitting there before the gesture -- which is what stops a green from being a residue of the
/// example before. There is no control leg in this group and no honest way to build one: the call
/// under test is a statement *inside* `PerformanceView`, not a separate call the example could
/// choose to omit the way `menu_action_open_perform`'s control omits `dispatchShellActions()`. The
/// counterpart here is the deletion mutation, reported with this task.
void enter_perform_view(bool default_mode, bool editing_param, PerformanceEditingMode stale) {
	currentUIMode = UI_MODE_NONE;
	setRootUILowLevel(&perform_exit_stub());
	(void)openUI(&performanceView);
	performanceView.defaultEditingMode = default_mode;
	performanceView.editingParam = editing_param;
	menu::set_performance_editing_mode(stale);
}

/// @brief The mirror and `readCurrentValue`'s answer for the view as it stands, as one value.
///
/// Both, deliberately, and each example compares the pair against a LITERAL mode.
/// `performance_editing_mode() == mode_from_view()` on its own says the two agree but not on what,
/// and stays green with `VALUE` and `PARAM` swapped on both sides -- the literal is what pins the
/// mapping the brief warns about. A free function rather than two `expect` calls in a helper,
/// because `expect` is `ItBase::expect` and only exists inside an example.
std::array<int, 2> mirror_and_view() {
	return {static_cast<int>(menu::performance_editing_mode()), static_cast<int>(mode_from_view())};
}

/// @brief `mirror_and_view()`'s answer when both halves are @p mode -- the expected value.
/// (`both` on its own is ambiguous against a name the spec headers already pull in.)
std::array<int, 2> both_at(PerformanceEditingMode mode) {
	return {static_cast<int>(mode), static_cast<int>(mode)};
}

// --- F2 task 6: the shortcuts-layout setting, in both directions -------------------------------

/// @brief Put back everything `SoundEditor::setShortcutsVersion` moves, on both sides of the wall.
///
/// Unlike `shortcuts_spec.cpp`'s `LegacyVersion1` -- which this is otherwise a copy of, cells and
/// all -- the constructor **selects nothing**. Each example drives the choice itself, because the
/// choice is what is under test.
///
/// The twelve cells matter because `setShortcutsVersion`'s `SHORTCUTS_VERSION_1` arm assigns them in
/// place (`gui/ui/sound_editor.cpp:157-173`) and its `default` arm is a comment, not an undo
/// (`:177-179`) -- so a V1 -> V3 round trip does **not** restore them and the other twenty specs in
/// this process would see a corrupted legacy shortcut table. `menu::set_shortcuts_version` has no
/// such problem (it selects between two `constexpr` layers), but it is saved here too so one guard
/// covers the whole setting.
struct ShortcutsVersionGuard {
	::MenuItem* sounds[6];
	::MenuItem* clips[4];
	PatchSource sources[2];
	uint8_t legacy_version;
	menu::ShortcutsVersion port_version;

	ShortcutsVersionGuard(const ShortcutsVersionGuard&) = delete;
	ShortcutsVersionGuard& operator=(const ShortcutsVersionGuard&) = delete;

	ShortcutsVersionGuard() {
		for (size_t i = 0; i < 6; i++) {
			sounds[i] = paramShortcutsForSounds[kSoundCells[i].first][kSoundCells[i].second];
		}
		for (size_t i = 0; i < 4; i++) {
			clips[i] = paramShortcutsForAudioClips[kClipCells[i].first][kClipCells[i].second];
		}
		sources[0] = modSourceShortcuts[0][7];
		sources[1] = modSourceShortcuts[1][7];
		legacy_version = soundEditor.shortcutsVersion;
		port_version = menu::shortcuts_version();
	}

	~ShortcutsVersionGuard() {
		for (size_t i = 0; i < 6; i++) {
			paramShortcutsForSounds[kSoundCells[i].first][kSoundCells[i].second] = sounds[i];
		}
		for (size_t i = 0; i < 4; i++) {
			paramShortcutsForAudioClips[kClipCells[i].first][kClipCells[i].second] = clips[i];
		}
		modSourceShortcuts[0][7] = sources[0];
		modSourceShortcuts[1][7] = sources[1];
		// The field, not the setter: the setter would rewrite the arrays this destructor has just
		// restored.
		soundEditor.shortcutsVersion = legacy_version;
		menu::set_shortcuts_version(port_version);
	}

private:
	static constexpr std::pair<uint8_t, uint8_t> kSoundCells[6] = {{0, 6}, {1, 6}, {2, 6}, {3, 6}, {2, 7}, {3, 7}};
	static constexpr std::pair<uint8_t, uint8_t> kClipCells[4] = {{0, 7}, {1, 7}, {0, 6}, {1, 6}};
};

/// The two values the ported item can hold, as its own enum indices -- which are legacy's numbers
/// (`SHORTCUTS_VERSION_1` 0, `SHORTCUTS_VERSION_3` 1, `gui/ui/sound_editor.h:26-27`; restated by
/// `menu::ShortcutsVersion`, `scenes/menu/shortcuts.h:154-157`). Spelled here as the *item's* index
/// rather than reached for through either enum, because what the examples drive is a cursor.
constexpr int32_t kVersion1Index = 0;
constexpr int32_t kVersion3Index = 1;

/// @brief Open the ported Pads page, step into `shortcuts_version`, and turn the encoder until the
///        item sits on @p index -- leaving whatever the write filed **in the outbox**.
///
/// The whole gesture is real input through the real widget: a row press to descend (legacy's own
/// `shouldEnterSubmenu()`, which for a bare `Selection` is the base's `return true`,
/// `gui/menu_item/menu_item.h:271`) and then detents, one per tick, which is what the value widget's
/// clamping walk expects. The write happens on the turn and not on a further press, because that is
/// where legacy's happens: `SoundEditor::selectEncoderAction` is what reaches `writeCurrentValue`.
///
/// @return The enum cursor the item ended on, so an example can say the turn really landed rather
///         than assuming it.
int32_t choose_shortcuts_version(Rig& r, int32_t index) {
	(void)r.open(nodes::pads_settings);
	r.shell.tick(5);
	(void)scene().take_action(); // opening the page files nothing; the group asserts that separately
	r.enter(10);                 // row 0 is `shortcuts_version`
	r.shell.tick(20);
	(void)scene().take_action(); // nor does descending into the leaf

	int32_t cursor = static_cast<int32_t>(scene().state().enum_index);
	for (uint32_t step = 0; cursor != index && step < 8; step++) {
		r.turn(index > cursor ? 1 : -1, 30 + step * 10);
		r.shell.tick(35 + step * 10);
		cursor = static_cast<int32_t>(scene().state().enum_index);
	}
	return cursor;
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
		expect(first.action == MenuAction::StartStemExport).to_be_true();
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
		// `retarget` drops `MenuState::pending_action`, but the drain has already moved this one into
		// the scene's own outbox, which it must not drop -- see `MenuScene::taken_action_`.
		menu_scene().open(nodes::root_song, ctx);
		expect(scene().take_action().action == MenuAction::StartStemExport).to_be_true();
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
		// **E3 task 7 fix round 1 reversed this example, and the reversal is the point.** It used to
		// assert that a `SceneId::arg` deep link onto one of these items filed its action; that was
		// the port firing `selectButtonPress` with no press behind it. Legacy cannot: it never
		// navigates into these items at all (`shouldEnterSubmenu() == false`,
		// `gui/ui/sound_editor.cpp:420-423`), so the only way to reach the body is a press on the
		// parent's row, and `Toggle` -- the same shape -- guards the other way in explicitly
		// (`gui/menu_item/toggle.h:35-37`). `MenuScene::enter_leaf`'s `FocusCause` is that
		// discriminator and `CustomItem::on_focus_is_press` is its scope.
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
		expect(scene().focused() == &nodes::start_stem_export).to_be_true();
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
			expect(item != nullptr).to_be_true();
			observed.push_back(item != nullptr ? static_cast<int>(item->action()) : -1);
		}
		std::vector<int> expected;
		for (const Pairing& p : kVerbs) {
			expected.push_back(static_cast<int>(p.action));
		}
		expect(observed).to_equal(expected);

		// ... and a single transposition of it must NOT compare equal, or the assertion above could
		// be passing on an all-`None` list. Task 11's pad-colour example carries the same proof.
		std::vector<int> swapped = observed;
		std::swap(swapped[0], swapped[2]);
		expect(swapped == expected).to_be_false();

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
			// Legacy's `shouldEnterSubmenu() == false` (`gui/ui/sound_editor.cpp:420-423`): the item
			// is never navigated into. The port descends for the width of one `after_render` and
			// ascends inside it, so by the time the tick is over the parent is focused again.
			expect(scene().focused() == p.parent).to_be_true();
			expect(static_cast<int>(scene().state().depth)).to_equal(0);
		}
	});
});

describe menu_action_perform("scenes::menu PerformEditorItem", $ {
	it("offers exactly the options the real legacy EditingMode offers", _ {
		select_language();
		Rig r;
		(void)r.open(nodes::root_performance);

		// Both sides driven: legacy's own list, from the real singleton...
		const auto legacy = performEditorMenu.getOptions(deluge::gui::menu_item::Selection::OptType::FULL);
		std::vector<std::string> legacy_names;
		for (const std::string_view v : legacy) {
			legacy_names.emplace_back(v);
		}
		// ... and the port's, resolved through the same l10n table the item uses.
		const std::vector<std::string> port_names{
		    std::string(text_lookup(static_cast<uint16_t>(String::STRING_FOR_DISABLED))),
		    std::string(text_lookup(static_cast<uint16_t>(String::STRING_FOR_PERFORM_EDIT_VALUE))),
		    std::string(text_lookup(static_cast<uint16_t>(String::STRING_FOR_PERFORM_EDIT_PARAM))),
		};
		expect(port_names).to_equal(legacy_names);
	});

	it("maps the performance view's two bools onto a mode exactly as legacy's readCurrentValue does", _ {
		select_language();
		PerformViewGuard guard;

		// All three reachable states of the pair. Legacy is *driven* for each -- `readCurrentValue`
		// is what fills its value, and skipping the call would compare against whatever the previous
		// example left behind (the trap task 11 fix round 1 recorded).
		const std::array<std::pair<bool, bool>, 3> states{{{false, false}, {true, false}, {true, true}}};
		for (const auto& [default_mode, editing_param] : states) {
			performanceView.defaultEditingMode = default_mode;
			performanceView.editingParam = editing_param;

			performEditorMenu.readCurrentValue();
			const int32_t legacy_value = performEditorMenu.getValue();

			expect(static_cast<int>(mode_from_view())).to_equal(static_cast<int>(legacy_value));
		}
		// `{false, true}` is unreachable in legacy -- `readCurrentValue` tests `defaultEditingMode`
		// first -- and maps to DISABLED under the same rule, so it is asserted rather than skipped.
		performanceView.defaultEditingMode = false;
		performanceView.editingParam = true;
		performEditorMenu.readCurrentValue();
		expect(static_cast<int>(mode_from_view()))
		    .to_equal(static_cast<int>(performEditorMenu.getValue()));
	});

	it("seeds its cursor from the mirrored mode, the way beginSession seeds from the view", _ {
		select_language();
		PerformViewGuard guard;
		for (const auto mode :
		     {PerformanceEditingMode::DISABLED, PerformanceEditingMode::VALUE, PerformanceEditingMode::PARAM}) {
			Rig r;
			menu::set_performance_editing_mode(mode);
			(void)r.open(nodes::perform_editor);
			r.shell.tick(10);
			expect(static_cast<int>(scene().state().list[0].cursor)).to_equal(static_cast<int>(mode));
			expect(scene().take_action().engaged()).to_be_false(); // opening files nothing
		}
	});

	it("files OpenPerformanceView carrying the chosen mode, and stays on the page", _ {
		select_language();
		for (const auto mode : {PerformanceEditingMode::VALUE, PerformanceEditingMode::PARAM}) {
			PerformViewGuard guard;
			Rig r;
			menu::set_performance_editing_mode(PerformanceEditingMode::DISABLED);
			(void)r.open(nodes::perform_editor);
			r.shell.tick(5);
			for (int32_t i = 0; i < static_cast<int32_t>(mode); i++) {
				r.turn(1, static_cast<uint32_t>(10 + i * 10));
				r.shell.tick(static_cast<uint32_t>(15 + i * 10));
			}
			r.enter(100);
			r.shell.tick(110);

			const MenuActionRequest got = scene().take_action();
			expect(got.action == MenuAction::OpenPerformanceView).to_be_true();
			expect(static_cast<int>(got.arg)).to_equal(static_cast<int>(mode));
			// `:55-60` -- the press is what writes the two bools legacy keeps on the view.
			expect(static_cast<int>(menu::performance_editing_mode())).to_equal(static_cast<int>(mode));
			// `:73` -- `NO_NAVIGATION`: legacy does not leave this page.
			expect(scene().focused() == &nodes::perform_editor).to_be_true();
		}
	});

	it("goes up a level on DISABLED and files nothing, which is legacy's return nullptr", _ {
		select_language();
		PerformViewGuard guard;
		Rig r;
		menu::set_performance_editing_mode(PerformanceEditingMode::VALUE);
		const MenuContext ctx = r.open(nodes::root_performance);
		const int32_t index = visible_index_of(nodes::root_performance, nodes::perform_editor, ctx);
		expect(index).to_equal(0);
		r.shell.tick(5);
		r.enter(10);
		r.shell.tick(20);
		expect(scene().focused() == &nodes::perform_editor).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();

		// Back to DISABLED, then press.
		r.turn(-1, 30);
		r.shell.tick(40);
		expect(static_cast<int>(scene().state().list[1].cursor))
		    .to_equal(static_cast<int>(PerformanceEditingMode::DISABLED));
		r.enter(50);
		r.shell.tick(60);

		expect(scene().take_action().engaged()).to_be_false();
		expect(scene().focused() == &nodes::root_performance).to_be_true();
		// And the mirror is untouched -- legacy's DISABLED arm writes nothing either.
		expect(static_cast<int>(menu::performance_editing_mode()))
		    .to_equal(static_cast<int>(PerformanceEditingMode::VALUE));
	});

	it("files nothing across twenty quiet ticks while sitting on a non-DISABLED option", _ {
		select_language();
		PerformViewGuard guard;
		Rig r;
		menu::set_performance_editing_mode(PerformanceEditingMode::PARAM);
		(void)r.open(nodes::perform_editor);
		for (uint32_t t = 0; t < 20; t++) {
			r.shell.tick(10 + t * 10);
			expect(scene().take_action().engaged()).to_be_false();
		}
	});
});

describe menu_action_dispatch("gui::ui::dispatchShellActions over the real outbox", $ {
	// The other side of the seam, in the binary that has every half: `deluge_scenes_spec` links the
	// real `menu_scene()`, the real `dispatchShellActions` AND the real
	// `ShellHostUI::timerCallback` (`nm -C` on the driver shows all three as `T`). The host suite's
	// `tests/spec/gui/shell_action_dispatch_spec.cpp` links only the last of those -- it stubs the
	// dispatcher, because the real one belongs to the `deluge_scenes` closure that suite never
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
			expect(scene().take_action().action == MenuAction::StartStemExport).to_be_true();
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
		expect(scene().take_action().action == MenuAction::StartStemExport).to_be_true();
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
			expect(scene().take_action().action == MenuAction::StartStemExport).to_be_true();
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

describe menu_action_open_perform("gui::ui::dispatchShellActions -- OpenPerformanceView", $ {
	// The first arm with a body (F2 task 2), and the only group in this file that asserts a legacy
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
	//   * **the open** -- `isUIOpen(&performanceView)`, and NOT `getCurrentUI()`, which answers
	//     `lastUIBeforeNullifying` on an empty hierarchy (`gui/ui/ui.cpp:136-140`) and so cannot
	//     tell "opened" from "an earlier example left this behind".
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
			expect(filed.action == MenuAction::OpenPerformanceView).to_be_true();
			expect(static_cast<int>(filed.arg)).to_equal(static_cast<int>(PerformanceEditingMode::VALUE));
		}

		Rig r;
		seed_view_away_from(PerformanceEditingMode::VALUE);
		press_perform_mode(r, PerformanceEditingMode::VALUE);
		deluge::gui::ui::dispatchShellActions();

		// `editing_mode.h:55-58` -- the VALUE branch.
		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_false();
		// `editing_mode.h:86-93` -- `!editingParam`, so the reset runs.
		expect(performanceView.lastPadPress.isActive).to_be_false();
		// `editing_mode.h:95` -- `openUI(&performanceView)`.
		expect(isUIOpen(&performanceView)).to_be_true();
		// And the outbox is empty, so the effects above came from this one request rather than from
		// a second delivery of it.
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("PARAM: writes both bools and opens the view, and does NOT reset it", _ {
		// Legacy's own reason, at `editing_mode.h:87-88`: resetting in param-editing mode "would
		// reset param assignments to FX columns". This is the half of the guard that an
		// implementation which simply always resets would still pass the VALUE example with.
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
			expect(filed.action == MenuAction::OpenPerformanceView).to_be_true();
			expect(static_cast<int>(filed.arg)).to_equal(static_cast<int>(PerformanceEditingMode::PARAM));
		}

		Rig r;
		seed_view_away_from(PerformanceEditingMode::PARAM);
		press_perform_mode(r, PerformanceEditingMode::PARAM);
		deluge::gui::ui::dispatchShellActions();

		// `editing_mode.h:59-62` -- the PARAM branch.
		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_true();
		// `editing_mode.h:86` -- `editingParam`, so the reset is SKIPPED and the marker stands.
		expect(performanceView.lastPadPress.isActive).to_be_true();
		// `editing_mode.h:95` -- the open happens either way; only the reset is conditional.
		expect(isUIOpen(&performanceView)).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("reaches the view through the guest's own tick, with nothing calling the dispatcher by name", _ {
		// Task 1's composition example, now that there is an effect to see at the end of it: a real
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
		// `GuestRig` has no encoder helper, so the cursor is placed by the mirror instead of walked:
		// `PerformEditorItem::on_focus` seeds it from `performance_editing_mode()`
		// (`nodes/actions.cpp:121`), which is `EditingMode::beginSession` -> `readCurrentValue`.
		// VALUE rather than DISABLED, because DISABLED is the arm that files nothing.
		{
			GuestRig control;
			menu::set_performance_editing_mode(PerformanceEditingMode::VALUE);
			expect(control.open(nodes::perform_editor)).to_be_true();
			control.ui.shell().tick(5);
			seed_view_away_from(PerformanceEditingMode::VALUE);
			control.press_enter();
			control.ui.shell().tick(20);

			expect(performanceView.defaultEditingMode).to_be_false();
			expect(isUIOpen(&performanceView)).to_be_false();
			expect(scene().take_action().action == MenuAction::OpenPerformanceView).to_be_true();
		}

		GuestRig r;
		menu::set_performance_editing_mode(PerformanceEditingMode::VALUE);
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
	// The second arm with a body (F2 task 3), and the one `scenes/menu/action.h:107-109` deliberately
	// refused to split into four enumerators, "the choice is made from the root UI, which this layer
	// cannot read". So the fan-out over `getRootUI()` (`gui/menu_item/stem_export/start.h:33-47`) is
	// the whole of what this side of the seam adds, and it is what this group pins.
	//
	// **Why the chooser is driven by name and the arm is not, which is a real limit and not a
	// preference.** `StemExport::startStemExportProcess` hands the export to
	// `deluge::storage::Owner::run` (`processing/stem_export/stem_export.cpp:87-96`), and on the
	// cooperative BSP this binary links, `Owner::run` runs the op **inline on the caller's stack**
	// (`OSLikeStuff/task_scheduler/task_scheduler_c_api.cpp:101-104`). An example that let a
	// matching arm reach it would arm the recorder, write stems under `DELUGE_SD_ROOT` and then sit
	// in `renderWait` (`stem_export.cpp:587-596`) waiting for a transport no spec is driving. So the
	// four matching arms are driven through `stemExportTypeForRootUI()`, and the dispatcher's arm is
	// driven end to end only where the chain falls through -- the branch that starts nothing.
	//
	// **The set these examples cover is the whole of legacy's chain**, stated so a reader can check
	// it: `arrangerView` twice (`start.h:34-41` is one root with two answers), `sessionView` once
	// (`:42-44`), `instrumentClipView` twice (`:45-47`'s `&&`, once each way), plus the two ways to
	// reach no arm at all -- a root legacy never names, and no root open. Six root states, five
	// answers, and every `if` in `start.h:33-47` has an example on both sides of it.
	//
	// Each example seeds `currentStemExportType` at a value the answer is not, so no assertion can
	// pass on a residue -- `StemExportGuard` puts it back.

	it("arrangerView with exportMixdown set chooses MIXDOWN", _ {
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_synth_song();

		// `setRootUILowLevel` (`gui/ui/ui.cpp:122-127`) is legacy's own "seat the root, run no
		// `opened()`" -- what `setupBlankSong` uses. `changeRootUI` (`:107-119`) would run the real
		// view's `opened()`, which is a great deal of view work for a fact the chooser answers with
		// one pointer comparison.
		setRootUILowLevel(&arrangerView);
		stemExport.exportMixdown = true;

		expect(chosen_stem_export_type()).to_equal(static_cast<int>(StemExportType::MIXDOWN)); // `start.h:35-37`
	});

	it("arrangerView without exportMixdown chooses TRACK", _ {
		// The other half of the same `if` (`start.h:38-40`). Identical to the example above in every
		// statement but the one `exportMixdown` assignment, which is what makes the pair say that
		// the flag is read rather than that the arranger has a fixed answer.
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_synth_song();

		setRootUILowLevel(&arrangerView);
		stemExport.exportMixdown = false;

		expect(chosen_stem_export_type()).to_equal(static_cast<int>(StemExportType::TRACK)); // `start.h:38-40`
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

		expect(chosen_stem_export_type()).to_equal(static_cast<int>(StemExportType::CLIP)); // `start.h:42-44`
	});

	it("instrumentClipView over a KIT chooses DRUM", _ {
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		// The output type is the second half of `start.h:45`'s `&&`, and `getCurrentOutputType()`
		// reads it off the song's current clip (`model/song/song.cpp:135-137`) -- so the fixture is
		// what varies between this example and the one below, not the root.
		spec::SongFixture song = spec::make_kit_song();
		expect(getCurrentOutputType() == OutputType::KIT).to_be_true();

		setRootUILowLevel(&instrumentClipView);

		expect(chosen_stem_export_type()).to_equal(static_cast<int>(StemExportType::DRUM)); // `start.h:45-47`
	});

	it("instrumentClipView over a non-KIT output chooses nothing, which is legacy's fall-through", _ {
		// `start.h:45`'s `&&` in the direction that fails, and `start.h:48`: legacy returns
		// `NO_NAVIGATION` having started no export. Same root as the example above, different
		// output -- the one difference.
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_synth_song();
		expect(getCurrentOutputType() == OutputType::SYNTH).to_be_true();

		setRootUILowLevel(&instrumentClipView);

		expect(chosen_stem_export_type()).to_equal(-1);
	});

	it("a root legacy never names, and no root at all, both choose nothing", _ {
		UiStackGuard ui_guard;
		StemExportGuard stem_guard;
		spec::SongFixture song = spec::make_kit_song();
		stemExport.exportMixdown = true;

		// A real `RootUI` that `start.h:33-47` simply does not test for. A KIT song with
		// `exportMixdown` set, so both of the facts the other arms read are true here and neither
		// can carry this one on its own.
		setRootUILowLevel(&performanceView);
		expect(chosen_stem_export_type()).to_equal(-1);

		// And the null root: `getRootUI()` answers `nullptr` while `numUIsOpen == 0`
		// (`gui/ui/ui.cpp:145-150`), which is legacy's "during song load / swap" (`:64`).
		// `nullifyUIs()` (`:257-261`) is how legacy itself gets there.
		nullifyUIs();
		expect(getRootUI() == nullptr).to_be_true();
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
			expect(scene().take_action().action == MenuAction::StartStemExport).to_be_true();
		}

		Rig r;
		// `Rig` builds its own `make_synth_song()`, so this root is `start.h:45`'s `&&` failing.
		setRootUILowLevel(&instrumentClipView);
		expect(chosen_stem_export_type()).to_equal(-1);
		stemExport.processStarted = false;

		(void)r.open(nodes::stem_export);
		r.shell.tick(5);
		r.enter(10);
		r.shell.tick(20);
		deluge::gui::ui::dispatchShellActions();

		// Nothing was exported. `runStemExportProcess` sets `processStarted` as its third statement
		// (`processing/stem_export/stem_export.cpp:102-103`) and on this BSP it would have run
		// inline, before this line (`task_scheduler_c_api.cpp:101-104`).
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

	it("takes the ported menu off the guest's stack, which is exitCompletely()'s half", _ {
		// `soundEditor.exitCompletely()` (`gui/menu_item/stem_export/start.h:32`) is the first
		// statement of legacy's body, and under the guest what is open is `ShellHostUI` rather than
		// `soundEditor`. The dispatcher's answer is `SceneStack::remove(SceneKind::FirstApp)` on the
		// guest's own stack -- the same statement `MenuScene::handle` already uses for the short-Back
		// exit, which IS `exitCompletely()` reached through `SoundEditor::goUpOneLevel`
		// (`scenes/menu/menu_scene.cpp:1459-1464`; `gui/ui/sound_editor.cpp:710-717`, the call at
		// `:716`). The full argument, including why neither `closeUI` nor `closeGuest()` can be
		// called from there, is at `closePortedMenu` in `gui/ui/shell_action_dispatch.cpp`.
		//
		// The guest is ticked through `Shell::tick` and NOT through `timerCallback`, on purpose: it
		// is `timerCallback`'s empty-stack arm (`gui/ui/shell_host_ui.cpp:526-529`) that performs
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
			(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true, false);
			shellHostUI.shell().tick(20);

			expect(shellHostUI.shell().stack().depth()).to_equal(size_t(1));
			expect(scene().take_action().action == MenuAction::StartStemExport).to_be_true();
		}

		GlobalGuestGuard guest_guard;
		spec::SongFixture song = spec::make_synth_song();
		// The fall-through root again, so this example is about the close and nothing else.
		setRootUILowLevel(&instrumentClipView);
		expect(chosen_stem_export_type()).to_equal(-1);

		expect(open_global_guest(nodes::stem_export, song)).to_be_true();
		shellHostUI.shell().tick(5);
		expect(shellHostUI.shell().stack().depth()).to_equal(size_t(1));
		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true, false);
		shellHostUI.shell().tick(20);
		// Still up after the tick that filed the request: the scene does not pop itself
		// (`scenes/menu/nodes/actions.h:47-50`), which is why the dispatcher owes this at all.
		expect(shellHostUI.shell().stack().depth()).to_equal(size_t(1));

		deluge::gui::ui::dispatchShellActions();

		expect(shellHostUI.shell().stack().depth()).to_equal(size_t(0));
		expect(scene().take_action().engaged()).to_be_false();
	});
});

describe menu_action_three_verbs("gui::ui::dispatchShellActions -- the three one-line verbs", $ {
	// The last three arms (F2 task 4). Each is one legacy statement -- or, for the two context
	// menus, one legacy *pair* -- so they share a group rather than getting three of their own.
	//
	// **All three run the real legacy callee, and none of them needs a decision function.** That
	// was not obvious: `StartStemExport` had to be split (`stemExportTypeForRootUI`) because its
	// acting half runs a real export inline on this BSP. None of these three does anything
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
	// and `previousGridModeActive` are private (`gui/views/session_view.h:231`, `:202`), so the half
	// of `enterMidiLearnMode`/`enterMacrosConfigMode` that moves them is asserted only through the
	// state that *is* reachable -- `currentUIMode` and `currentSong->sessionLayout`.
	//
	// @par What these examples do NOT pin, and why that is a fact about legacy rather than a hole
	//
	// **The ORDER of the two statements in each context-menu arm.** Swapping them -- `openUI(...)`
	// first, `setupAndCheckAvailability()` second -- leaves this group green; measured, not assumed.
	// The arms are written in legacy's order because they are a transcription
	// (`gui/menu_item/song/midi_learn.h:28-29`), and it is worth knowing that no test defends it.
	//
	// It is not merely unobserved, though. **It is unobservable**, and the reason is every effect
	// `openUI` has, not just the one that is easy to name. `openUI` (`gui/ui/ui.cpp:227-245`) does
	// five things, and each is inert with respect to `setupAndCheckAvailability`:
	//
	//   1. **The push** (`:230-231`), which happens FIRST inside `openUI` -- before any of the four
	//      below -- so nothing here observes a half-open stack either way.
	//   2. **`uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC)`** (`:233`). A *different* timer from
	//      the only one this arm's other statement arms: `View::midiLearnFlash` sets
	//      `TimerName::MIDI_LEARN_FLASH` (`gui/views/view.cpp:815`). Neither clears the other.
	//   3. **`PadLEDs::reassessGreyout()`** (`:234`). **This is the one that could have made the
	//      order observable, which is why it belongs in the argument rather than being left out of
	//      it.** It recomputes from `getUIGreyoutColsAndRows` (`gui/ui/ui.cpp:73-83`), which walks the
	//      hierarchy top-down and takes the first UI that answers true -- and by `:234` the context
	//      menu is already top, in either order. `MidiLearnMode::getGreyoutColsAndRows`
	//      (`gui/context_menu/midi_learn_mode.cpp:32-36`) then answers two **constants** and reads no
	//      state at all, `currentUIMode` included. Had that greyout depended on the MIDI-learn mode,
	//      the two orders would give different pad greyouts and this example would be hiding a real
	//      difference.
	//   4. **`newUI->opened()`** (`:235`). Neither context menu overrides it, so it is `UI::opened`
	//      (`gui/ui/ui.h:111-114`): `focusRegained()` -- for a `ContextMenu`, one
	//      `blinkLed(IndicatorLED::BACK)` (`gui/context_menu/context_menu.cpp:54-56`) -- and then
	//      `return true`, **unconditionally**. That last word matters: the `!success` branch
	//      (`gui/ui/ui.cpp:237-242`) pops the UI and calls `oldUI->focusRegained()`, which WOULD be
	//      order-sensitive, and it is unreachable here for that reason rather than by luck. Nothing
	//      on the path -- `getTitle`/`getOptions` included (`midi_learn_mode.cpp:38-48`) -- reads
	//      `currentUIMode`, and the LED is not the `IndicatorLED::LEARN` that `View::startMIDILearn`
	//      blinks (`gui/views/view.cpp:495`).
	//   5. **`renderUIsForOled()`** (`:243`), which sets `doesOLEDNeedRendering = true` and nothing
	//      else (`:263-265`). No frame is composed inside `openUI`, so there is no transient render
	//      in which the mode could differ.
	//
	// And the reverse direction is closed too, which the list above does not cover on its own:
	// `View::midiLearnFlash` calls `getRootUI()->midiLearnFlash()` (`gui/views/view.cpp:817-819`), so
	// running `setupAndCheckAvailability()` *after* the open would be observable if the open moved
	// the root. It cannot: `getRootUI()` reads `uiNavigationHierarchy[0]` (`gui/ui/ui.cpp:145-150`)
	// and `openUI` writes index `numUIsOpen` (`:230`), which is >= 1 whenever a root view is open --
	// the precondition `stemExportTypeForRootUI`'s own note in `gui/ui/shell_action_dispatch.cpp`
	// states and these examples establish with `setRootUILowLevel(&sessionView)`.

	it("SetClipLengthToSampleLength: sets the clip's length from the sample, as legacy's view does", _ {
		select_language();
		UiStackGuard ui_guard;
		spec::QuietAudio quiet;

		{
			// The control. One call different.
			DisplayGuard control_display;
			ActionLogGuard control_log;
			Rig control;
			const spec::AudioSongFixture f = seat_audio_clip_with_sample();
			setRootUILowLevel(&audioClipView);
			const int32_t seeded = f.clip->loopLength;

			expect(press_child_leaving_request(control, nodes::audio_clip_actions, nodes::set_clip_length))
			    .to_be_true();

			expect(f.clip->loopLength).to_equal(seeded);
			expect(static_cast<int>(control_display.recorder.popups.size())).to_equal(0);
			expect(scene().take_action().action == MenuAction::SetClipLengthToSampleLength).to_be_true();
		}

		DisplayGuard display_guard;
		ActionLogGuard log_guard;
		Rig r;
		const spec::AudioSongFixture f = seat_audio_clip_with_sample();
		// `audioClipView` as the root, which is the view legacy's other caller of this same method
		// runs inside (`gui/views/audio_clip_view.cpp:384-393`, the call at `:391`) and the view the
		// audio-clip menu is opened over. It also has to be *something*: `adjustLoopLength` hands
		// `getRootUI()` to `uiNeedsRendering` (`:783`).
		setRootUILowLevel(&audioClipView);

		const int32_t seeded = f.clip->loopLength;
		// Legacy's own argument, from legacy's own function -- `audio_clip_view.cpp:763` computes
		// exactly this and passes it to `adjustLoopLength`. Read here rather than hard-coded so the
		// expectation is derived from the model both sides share.
		const int32_t from_sample = f.clip->sampleHolder.getLoopLengthAtSystemSampleRate(true);
		// ...and the two differ, which is what stops the assertion below passing on a no-op. Without
		// the tempo `seat_audio_clip_with_sample` writes, this is the line that goes red.
		expect(from_sample == seeded).to_be_false();

		expect(press_child_leaving_request(r, nodes::audio_clip_actions, nodes::set_clip_length)).to_be_true();
		deluge::gui::ui::dispatchShellActions();

		// `set_clip_length_equal_to_sample_length.h:27` -> `audio_clip_view.cpp:758-769`: the
		// `sampleHolder` branch (`:762-765`), whose two statements are the length and the popup.
		expect(f.clip->loopLength).to_equal(from_sample);
		expect(static_cast<int>(display_guard.recorder.popups.size())).to_equal(1);
		expect(display_guard.recorder.popups.empty() ? std::string{} : display_guard.recorder.popups[0])
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_CLIP_LENGTH_ADJUSTED)));
		expect(scene().take_action().engaged()).to_be_false();

		// **The `else` at `audio_clip_view.cpp:766-768` is unreachable and is therefore not covered
		// here.** It is guarded by `if (sampleHolder)` (`:762`), and `sampleHolder` is
		// `guide.audioFileHolder` (`:761`), which `AudioClip::AudioClip` points at the clip's own
		// `sampleHolder` member permanently (`model/clip/audio_clip.cpp:57`) -- the address of a
		// member, never null. So no `AudioClip` can take it, and `STRING_FOR_NO_SAMPLE` is dead
		// code. Reported, not fixed.
	});

	it("OpenSongMidiLearn: arms MIDI learn and opens the context menu, which is both statements", _ {
		select_language();
		UiStackGuard ui_guard;
		MidiLearnGuard learn_guard;

		{
			// The control. One call different.
			Rig control;
			// `sessionView` as the root, because that is the only root the item is relevant under
			// (`song::MidiLearn::isRelevant`, `gui/menu_item/song/midi_learn.h:35-37`) and because
			// `MidiLearnMode::setupAndCheckAvailability` acts on `sessionView` by name
			// (`gui/context_menu/midi_learn_mode.cpp:51`).
			setRootUILowLevel(&sessionView);
			// `View::startMIDILearn` only arms from `UI_MODE_NONE` or `UI_MODE_MIDI_LEARN`
			// (`gui/views/view.cpp:490`), so the seed is the mode under which legacy succeeds.
			currentUIMode = UI_MODE_NONE;
			expect(press_child_leaving_request(control, nodes::root_song, nodes::song_midi_learn)).to_be_true();

			expect(static_cast<int>(currentUIMode)).to_equal(static_cast<int>(UI_MODE_NONE));
			expect(isUIOpen(&deluge::gui::context_menu::midiLearnMode)).to_be_false();
			expect(scene().take_action().action == MenuAction::OpenSongMidiLearn).to_be_true();
		}

		Rig r;
		setRootUILowLevel(&sessionView);
		currentUIMode = UI_MODE_NONE;
		expect(press_child_leaving_request(r, nodes::root_song, nodes::song_midi_learn)).to_be_true();
		deluge::gui::ui::dispatchShellActions();

		// `midi_learn.h:28` -- `MidiLearnMode::setupAndCheckAvailability`
		// (`gui/context_menu/midi_learn_mode.cpp:50-53`) -> `SessionView::enterMidiLearnMode`
		// (`gui/views/session_view.cpp:3072-3077`) -> `View::startMIDILearn`
		// (`gui/views/view.cpp:489-497`), whose one globally visible write is
		// `currentUIMode = UI_MODE_MIDI_LEARN` (`:493`). It is also exactly what
		// `setupAndCheckAvailability` returns (`midi_learn_mode.cpp:52`), so this assertion says the
		// discarded bool was `true` here as well.
		expect(static_cast<int>(currentUIMode)).to_equal(static_cast<int>(UI_MODE_MIDI_LEARN));
		// `midi_learn.h:29` -- the `openUI` that runs whatever the bool said.
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
			control.song.song->sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
			expect(press_child_leaving_request(control, nodes::root_song, nodes::configure_song_macros)).to_be_true();

			expect(currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeRows).to_be_true();
			expect(isUIOpen(&deluge::gui::context_menu::configureSongMacros)).to_be_false();
			expect(scene().take_action().action == MenuAction::OpenSongMacros).to_be_true();
		}

		Rig r;
		setRootUILowLevel(&sessionView);
		// Seeded at the layout the effect is NOT, so the assertion below cannot pass on a value that
		// was already there. `Rig` owns this song and nothing outside the example reads it, so there
		// is no guard to put it back.
		r.song.song->sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
		expect(press_child_leaving_request(r, nodes::root_song, nodes::configure_song_macros)).to_be_true();
		deluge::gui::ui::dispatchShellActions();

		// `configure_macros.h:28` -- `ConfigureSongMacros::setupAndCheckAvailability`
		// (`gui/context_menu/configure_song_macros.cpp:48-51`) -> `SessionView::enterMacrosConfigMode`
		// (`gui/views/session_view.cpp:3060-3066`), whose one write to state this spec can read is
		// `currentSong->sessionLayout = ...Grid` (`:3062`). Its other two writes,
		// `previousLayout` and `gridModeActive`, are private members of `SessionView`.
		expect(currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid).to_be_true();
		// `configure_macros.h:29`.
		expect(isUIOpen(&deluge::gui::context_menu::configureSongMacros)).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});
});

describe menu_action_perform_mirror("PerformanceView -> menu::performance_editing_mode()", $ {
	// F2 task 5 -- the OTHER half of `OpenPerformanceView`. Every group above drives the port and
	// watches legacy move; this one drives LEGACY, with no menu and no `Shell` anywhere in it, and
	// watches the port's mirror keep up.
	//
	// **What is being pinned.** `scenes/menu/action.h`'s mirror is what `PerformEditorItem::on_focus`
	// seeds its cursor from (`menu/nodes/actions.cpp:121`), so it is authoritative for what the menu
	// SHOWS. It is not authoritative for the view, and `PerformanceView` moves the two bools it
	// mirrors from four places of its own -- and legacy's own menu item makes a fifth writer, which
	// review round 1 established is not excludable (see that group of examples at the end). One of
	// the five is a private helper reached from four different callers, so there are TEN examples
	// below and five sync calls under them:
	//
	//   | site | example(s) here |
	//   |---|---|
	//   | `buttonAction`'s BACK arm (`gui/views/performance_view.cpp:838-848`, the writes at `:840-841` and the call at `:844`) | 1 |
	//   | `buttonAction`'s shift+KEYBOARD exit (`:924-932`, the writes at `:925-926` and the call at `:928`) | 1 |
	//   | `buttonAction`'s shift+KEYBOARD cycle (`:933-956`, the writes at `:936` and `:938` and the call at `:942`) | 2 -- it is the only site *in the view* that can produce VALUE *or* PARAM |
	//   | `releaseViewOnExit` (`:1402-1412`, the writes at `:1405-1406` and the call at `:1411`) | 4 -- one per caller (`:748`, `:959`, `:977`, `:1092`) |
	//   | `EditingMode::selectButtonPress` (`gui/menu_item/performance_session_view/editing_mode.h:50-97`, the writes at `:56-57` and `:60-61`, the call at `:84`) | 2 -- both writing arms, and the DISABLED arm that must NOT sync |
	//
	// **The mapping, not just the movement.** `DISABLED`/`VALUE`/`PARAM` come from the PAIR
	// (`defaultEditingMode`, `editingParam`) under `EditingMode::readCurrentValue`
	// (`gui/menu_item/performance_session_view/editing_mode.h:35-46`), so an example that asserted
	// only "not DISABLED" would stay green with the two non-disabled modes swapped. Every example
	// asserts the two bools by name AND compares `mirror_and_view()` against a literal mode.
	//
	// **No control leg, and why there cannot be one.** In `menu_action_open_perform` the control is
	// the same rig minus `dispatchShellActions()`. Here the call under test is a statement inside
	// `PerformanceView::buttonAction`/`releaseViewOnExit`; an example cannot decline to run it while
	// still running the gesture. What plays the control's role is per-example and stated twice over:
	// each example parks the mirror at a mode the answer is NOT and asserts it is sitting there
	// immediately before the gesture (so no green is a residue), and each site was mutated by
	// deletion, which is reported with the task.
	//
	// **`getCurrentUI()` residue.** Same as `menu_action_open_perform`'s: this group opens real UIs
	// and `UiStackGuard`'s `nullifyUIs()` cannot put `lastUIBeforeNullifying` back. It is registered
	// after that group for the same reason, and reads `isUIOpen`/`getRootUI` rather than
	// `getCurrentUI` wherever it asserts about the stack.

	it("BACK out of editing mode syncs the mirror to DISABLED", _ {
		// `:838` -- `b == BACK && defaultEditingMode`, the arm reached only when
		// `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` is not latched (`:830` is tested first).
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/true, PerformanceEditingMode::PARAM);
		// The mirror is where the menu would have left it, and the view agrees -- so the assertion
		// after the press is about the press.
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));

		(void)performanceView.buttonAction(deluge::hid::button::BACK, true, false);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
	});

	it("shift+KEYBOARD out of param editing syncs the mirror to DISABLED", _ {
		// `:924-932` -- the exit half of the KEYBOARD arm, guarded by
		// `defaultEditingMode && editingParam` (`:924`).
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/true, PerformanceEditingMode::PARAM);
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));

		Buttons::commandToggleShift(true);
		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, true, false);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
	});

	it("shift+KEYBOARD into editing mode syncs the mirror to VALUE", _ {
		// `:933-938` with `defaultEditingMode` false: `inEditingMode` is false, `editingParam` is
		// left alone at `:935-937`, and `:938` sets `defaultEditingMode` -- which is
		// `readCurrentValue`'s VALUE (`editing_mode.h:39-41`). The half of the cycle that ENTERS.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/false, /*editing_param=*/false, PerformanceEditingMode::PARAM);
		// A mirror that is stale in the direction that matters: PARAM is what a VALUE answer must
		// move off, and it is also the wrong one of the two non-DISABLED modes.
		expect(static_cast<int>(menu::performance_editing_mode()))
		    .to_equal(static_cast<int>(PerformanceEditingMode::PARAM));

		Buttons::commandToggleShift(true);
		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, true, false);

		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_false();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::VALUE));
	});

	it("shift+KEYBOARD again, already in value editing, syncs the mirror to PARAM", _ {
		// The same site, the other way: `inEditingMode` is true at `:934`, so `:936` sets
		// `editingParam` before `:938` sets `defaultEditingMode`, and the pair maps to PARAM
		// (`editing_mode.h:42-44`). VALUE and PARAM from ONE site is what makes the swap this group's
		// header warns about visible -- a sync that reported the wrong one of the two would redden
		// exactly one of these two examples.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/false, PerformanceEditingMode::VALUE);
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::VALUE));

		Buttons::commandToggleShift(true);
		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, true, false);

		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_true();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));
	});

	// The four callers of `releaseViewOnExit`. The helper is private (`performance_view.h:154`), so
	// every one of these reaches it through the gesture that calls it -- which is the point:
	// `action.h` claimed three callers and there are four, and a sync call written into the helper
	// has to be seen from each.

	it("CLIP_VIEW out of editing mode syncs the mirror, through releaseViewOnExit", _ {
		// `:748`, the first caller. `lastClipInstanceEnteredStartPos` is set non-negative so `:750`
		// skips `sessionView.transitionToViewForClip()` (`:751`) -- the arm's own guard, not a stub: legacy
		// takes that branch whenever performance view was entered from the arranger.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/true, PerformanceEditingMode::PARAM);
		currentSong->lastClipInstanceEnteredStartPos = 0;
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));

		(void)performanceView.buttonAction(deluge::hid::button::CLIP_VIEW, true, false);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
	});

	it("KEYBOARD without shift syncs the mirror, through releaseViewOnExit", _ {
		// `:959`, the second caller -- the `else` of `Buttons::isShiftButtonPressed()` at `:923`.
		// `lastClipInstanceEnteredStartPos == -1` picks `changeRootUI(&sessionView)` (`:969`) of the
		// three exits, which is the one the arm takes from a session-view entry.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/true, PerformanceEditingMode::PARAM);
		currentSong->lastClipInstanceEnteredStartPos = -1;
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::PARAM));

		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, true, false);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
		// ...and it really was the exit branch, not the cycle branch above it.
		expect(getRootUI() == &sessionView).to_be_true();
	});

	it("releasing KEYBOARD after the hold time syncs the mirror, through releaseViewOnExit", _ {
		// `:977`, the third caller -- the "performance view was only peeked" release, gated on
		// `timeKeyboardShortcutPress != 0` (`:973`) and on the elapsed time clearing
		// `FlashStorage::holdTime` (`:976`). Both are set here rather than waited for.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/true, /*editing_param=*/false, PerformanceEditingMode::VALUE);
		currentSong->lastClipInstanceEnteredStartPos = -1;
		performanceView.timeKeyboardShortcutPress = 1;
		AudioEngine::audioSampleTimer = 1 + static_cast<uint32_t>(FlashStorage::holdTime);
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::VALUE));

		(void)performanceView.buttonAction(deluge::hid::button::KEYBOARD, false, false);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
		expect(getRootUI() == &sessionView).to_be_true();
	});

	it("a grid mode pad syncs the mirror, through releaseViewOnExit", _ {
		// `:1092`, the fourth caller -- and the one `action.h` did not list at all. Its arm is
		// guarded by `!defaultEditingMode` (`:1057`), so unlike the other three the VIEW is already at
		// DISABLED when the helper runs. That makes the call a no-op for a mirror that is already in
		// step, and the only thing it can move is a mirror that is NOT -- which is exactly the state
		// this task exists to stop shipping. So the seed here is a mirror deliberately out of step
		// with the view, and what the example says is "this path puts it back". Stated rather than
		// hidden: the seed is not a state legacy can reach on its own.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;

		enter_perform_view(/*default_mode=*/false, /*editing_param=*/false, PerformanceEditingMode::PARAM);
		currentSong->lastClipInstanceEnteredStartPos = -1; // `:1064` -- the session-view half
		currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeGrid; // `:1081` -- the grid half
		// Out of step by construction, and asserted as such.
		expect(static_cast<int>(menu::performance_editing_mode()))
		    .to_equal(static_cast<int>(PerformanceEditingMode::PARAM));
		expect(static_cast<int>(mode_from_view())).to_equal(static_cast<int>(PerformanceEditingMode::DISABLED));

		// `kDisplayWidth + 1` is the second sidebar column (`:1087`'s `else`), `GridMode::GREEN` one
		// of the two mode pads `:1091` names.
		(void)performanceView.padAction(kDisplayWidth + 1, GridMode::GREEN, 1);

		expect(performanceView.defaultEditingMode).to_be_false();
		expect(performanceView.editingParam).to_be_false();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::DISABLED));
		expect(getRootUI() == &sessionView).to_be_true();
	});

	// The fifth writer, and the only one outside `PerformanceView`: LEGACY's own Editing Mode item.
	// Task 5's first pass excluded it as "the menu, not behind the menu's back", which is true only
	// while the toggle is ON. With `RuntimeFeatureSettingType::ShellMenu` off, this item is how a
	// user reaches an editing mode from a menu at all (`gui/views/performance_view.cpp:882`'s
	// `else`), it writes both bools, and the mirror would sit where it was until the toggle is
	// flipped on and `PerformEditorItem::on_focus` reads it. Driven here on the REAL
	// `performEditorMenu` singleton, the same object the `menu_action_perform` group runs
	// `readCurrentValue` on.

	it("legacy's own Editing Mode item syncs the mirror on both of its writing arms", _ {
		// `editing_mode.h:55-62` -- the two arms that write, and their two answers. Both in one
		// example so a sync that reported the wrong one of the pair reddens here as well as on the
		// cycle site.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;
		const PerformanceEditingMode saved_item_mode = performEditorMenu.currentMode;

		for (const auto mode : {PerformanceEditingMode::VALUE, PerformanceEditingMode::PARAM}) {
			// The view where legacy leaves it with the toggle off, and the mirror where the port
			// left it: not agreeing, and not agreeing with the answer either.
			performanceView.defaultEditingMode = false;
			performanceView.editingParam = false;
			menu::set_performance_editing_mode(PerformanceEditingMode::DISABLED);
			expect(static_cast<int>(menu::performance_editing_mode()))
			    .to_equal(static_cast<int>(PerformanceEditingMode::DISABLED));

			// `currentMode` is what `writeCurrentValue` (`editing_mode.h:48`) leaves behind after the
			// encoder walk; set directly, because the walk is `Selection`'s and is not what is under
			// test here.
			performEditorMenu.currentMode = mode;
			auto* returned = performEditorMenu.selectButtonPress();

			expect(performanceView.defaultEditingMode).to_be_true();
			expect(performanceView.editingParam).to_equal(mode == PerformanceEditingMode::PARAM);
			expect(mirror_and_view()).to_equal(both_at(mode));
			// `editing_mode.h:96` -- `NO_NAVIGATION`, not `nullptr`; the arm really was a writing one.
			expect(returned != nullptr).to_be_true();
		}

		performEditorMenu.currentMode = saved_item_mode;
	});

	it("legacy's own Editing Mode item leaves the mirror alone on DISABLED, as the port does", _ {
		// `editing_mode.h:51-53` -- returns before writing either bool, so there is nothing to
		// mirror. The port's `PerformEditorItem` behaves the same way, which
		// `menu_action_perform`'s "goes up a level on DISABLED" example already pins; this is the
		// legacy half of that pair, and it is what says the fifth call sits AFTER the `if/else`
		// chain rather than at the top of the function.
		select_language();
		PerformViewGuard guard;
		UiStackGuard ui_guard;
		PerformGestureGuard gesture_guard;
		const PerformanceEditingMode saved_item_mode = performEditorMenu.currentMode;

		// A mirror and a view that already agree at VALUE, so "unchanged" is a real claim about
		// this press and not about a value that was never anything else.
		performanceView.defaultEditingMode = true;
		performanceView.editingParam = false;
		menu::set_performance_editing_mode(PerformanceEditingMode::VALUE);
		performanceView.lastPadPress.isActive = true; // `resetPerformanceView`'s marker, see `seed_view_away_from`

		performEditorMenu.currentMode = PerformanceEditingMode::DISABLED;
		auto* returned = performEditorMenu.selectButtonPress();

		expect(returned == nullptr).to_be_true(); // `editing_mode.h:52` -- "go up a level"
		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_false();
		expect(mirror_and_view()).to_equal(both_at(PerformanceEditingMode::VALUE));
		// ...and nothing else on that arm ran either: no reset, no open.
		expect(performanceView.lastPadPress.isActive).to_be_true();
		expect(isUIOpen(&performanceView)).to_be_false();

		performEditorMenu.currentMode = saved_item_mode;
	});
});

describe menu_action_shortcuts_version("gui::ui::dispatchShellActions -- SetShortcutsVersion", $ {
	// F2 task 6 -- the sixth arm, and the only one whose request is filed by a **value write**
	// rather than by a select press. `scenes/menu/action.h`'s "the sixth item" section says why:
	// legacy's `menu_item::shortcuts::Version` is an ordinary `Selection` the user opens and turns,
	// and what it cannot be ported is `writeCurrentValue`'s one statement,
	// `soundEditor.setShortcutsVersion(this->getValue())` (`gui/menu_item/shortcuts/version.h:27`).
	//
	// **What was broken, in user terms.** Until this arm existed the ported item wrote only this
	// layer's mirror (`menu::set_shortcuts_version`), so the menu showed the new layout and the
	// shortcut pads followed it -- and `SoundEditor::shortcutsVersion`, the byte
	// `FlashStorage::writeSettings` stores at byte 60 (`storage/flash_storage.cpp:982`) and
	// `readSettings` hands back at boot (`:568`), never moved. The layout survived until power-off
	// and no further. **Every control leg below is that bug**, reproduced deliberately: same rig,
	// same gesture, minus `dispatchShellActions()` -- and each one asserts the mirror DID move while
	// the legacy byte did not, which is the shape of the defect rather than merely "nothing
	// happened".
	//
	// **Flash itself is not written here, and that is deliberate.** `FlashStorage::writeSettings`
	// rewrites every setting this process shares with the other twenty specs in this binary -- the
	// reason `boot_wiring_spec.cpp` gives for not driving `resetSettings` either. What these
	// examples pin is `soundEditor.shortcutsVersion`, which is the *only* thing `:982` reads.
	//
	// **The reverse direction has its own example**, and it is not a duplicate of
	// `boot_wiring_spec.cpp`'s: it is this group's control for the mutation reported with the task.
	// Deleting the dispatcher arm must redden the new direction and leave the old one green, and
	// the two have to be in one file for that to be a comparison rather than a claim.
	//
	// `ShortcutsVersionGuard` is on every example: `setShortcutsVersion`'s V1 arm rewrites twelve
	// cells of the legacy shortcut tables in place and its V3 arm is a comment, not an undo.

	it("choosing V1 in the ported menu reaches SoundEditor::shortcutsVersion, which is flash byte 60", _ {
		select_language();
		ShortcutsVersionGuard guard;

		{
			// The control, and it differs in EXACTLY ONE CALL: no `dispatchShellActions()`. It is
			// also the bug this task closes, stated as an assertion: the mirror moves, the legacy
			// byte does not.
			soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);
			Rig control;
			expect(choose_shortcuts_version(control, kVersion1Index)).to_equal(kVersion1Index);

			expect(menu::shortcuts_version() == menu::ShortcutsVersion::V1).to_be_true();
			expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_3);
			// ...and the write really did file, so the subject's effect is about the arm and not
			// about a turn that never reached the hook.
			const MenuActionRequest filed = scene().take_action();
			expect(filed.action == MenuAction::SetShortcutsVersion).to_be_true();
			expect(static_cast<int>(filed.arg)).to_equal(kVersion1Index);
		}

		// Seeded at the value the answer is NOT, on both sides, so neither assertion below can pass
		// on a value that was already there.
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);
		expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_3);

		Rig r;
		expect(choose_shortcuts_version(r, kVersion1Index)).to_equal(kVersion1Index);
		deluge::gui::ui::dispatchShellActions();

		// `shortcuts/version.h:27` performed on the real `soundEditor` -- the byte
		// `storage/flash_storage.cpp:982` stores.
		expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_1);
		// The mirror is still right too. It was written by the hook and then written again, to the
		// same value, by `setShortcutsVersion`'s own mirror call -- the lap the enumerator's doc
		// argues terminates. If that lap were wrong in either direction this would be the value
		// that showed it.
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V1).to_be_true();
		// And the legacy table really moved, which is the effect a user sees: `(0, 6)` of the sound
		// shortcuts is `sample0EndMenu` under V1 (`gui/ui/sound_editor.cpp:163`).
		expect(paramShortcutsForSounds[0][6] == &sample0EndMenu).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("and back again: choosing V3 restores it, so the arm is not a one-way write", _ {
		// The other value byte 60 can hold. Without this an implementation that hard-coded V1 --
		// or one that only ever fired on a decrease -- would pass the example above.
		select_language();
		ShortcutsVersionGuard guard;

		{
			Rig control;
			soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_1);
			expect(choose_shortcuts_version(control, kVersion3Index)).to_equal(kVersion3Index);

			expect(menu::shortcuts_version() == menu::ShortcutsVersion::V3).to_be_true();
			expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_1);
			const MenuActionRequest filed = scene().take_action();
			expect(filed.action == MenuAction::SetShortcutsVersion).to_be_true();
			expect(static_cast<int>(filed.arg)).to_equal(kVersion3Index);
		}

		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_1);
		expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_1);

		Rig r;
		expect(choose_shortcuts_version(r, kVersion3Index)).to_equal(kVersion3Index);
		deluge::gui::ui::dispatchShellActions();

		expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_3);
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V3).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("does NOT ascend: the user is left standing on the item, as legacy leaves them", _ {
		// The `switch` in `MenuScene::after_render` that decides whether an action ascends. The four
		// verbs that file from `on_focus` do, because legacy never enters those items at all; this
		// one must not, because legacy's `Selection` IS entered and `writeCurrentValue` runs on
		// every detent while the user stands on it. An arm that ascended would close the page under
		// the user on the first turn.
		select_language();
		ShortcutsVersionGuard guard;

		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);
		Rig r;
		expect(choose_shortcuts_version(r, kVersion1Index)).to_equal(kVersion1Index);
		deluge::gui::ui::dispatchShellActions();

		// The first detent really wrote, so the second one below is a second write and not the
		// first one arriving late.
		expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_1);
		// Still inside the item, one level down from the Pads page it was opened at.
		expect(scene().focused() == &nodes::shortcuts_version).to_be_true();
		expect(static_cast<int>(scene().state().depth)).to_equal(1);
		// And a second detent still lands, which an ascend would have made impossible: the value
		// goes back to V3 and the byte follows it.
		r.turn(1, 200);
		r.shell.tick(210);
		deluge::gui::ui::dispatchShellActions();
		expect(static_cast<int>(scene().state().enum_index)).to_equal(kVersion3Index);
		expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_3);
		expect(scene().focused() == &nodes::shortcuts_version).to_be_true();
	});

	it("files nothing at all until the value is turned -- opening and descending are quiet", _ {
		// The other side of "a write, not a press". Walking onto the item and pressing into it is
		// what a user does before choosing anything, and it must not write flash.
		select_language();
		ShortcutsVersionGuard guard;

		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);
		Rig r;
		(void)r.open(nodes::pads_settings);
		r.shell.tick(5);
		expect(scene().take_action().engaged()).to_be_false();
		r.enter(10);
		r.shell.tick(20);
		expect(scene().focused() == &nodes::shortcuts_version).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
		// ...and it stays quiet across twenty ticks with the item open.
		for (uint32_t t = 0; t < 20; t++) {
			r.shell.tick(30 + t * 10);
			deluge::gui::ui::dispatchShellActions();
			expect(scene().take_action().engaged()).to_be_false();
		}
		expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_3);
	});

	it("reaches SoundEditor through the guest's own tick, with nothing calling the dispatcher by name", _ {
		// The composition, end to end: a real `ShellHostUI` over the real `scene_registry()`, a real
		// press and a real select-encoder turn through the guest's own hooks, and the guest's own
		// `timerCallback()` -- which is the only thing here that could write anything.
		// `dispatchShellActions` is never named.
		//
		// The two legs vary ONE THING -- shell tick vs guest tick -- the arrangement
		// `menu_action_open_perform`'s third example uses and for the same reason. It takes three
		// statements: both tick points differ, and the subject alone drains after its settle tick to
		// say that tick files nothing. That drain cannot affect what follows; the turn it would have
		// to eat is two lines later.
		select_language();
		ShortcutsVersionGuard guard;

		{
			GuestRig control;
			soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);
			expect(control.open(nodes::shortcuts_version)).to_be_true();
			control.ui.shell().tick(5);
			control.turn_select(-1);
			control.ui.shell().tick(20);

			expect(menu::shortcuts_version() == menu::ShortcutsVersion::V1).to_be_true();
			expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_3);
			expect(scene().take_action().action == MenuAction::SetShortcutsVersion).to_be_true();
		}

		GuestRig r;
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);
		// Opened AT the item rather than walked into, because `GuestRig` has no row-press helper for
		// a submenu row -- the same substitution `menu_action_open_perform`'s guest example makes.
		// A deep link onto an `Kind::Enum` leaf shows its page with the current value, which is what
		// a restore does; the turn after it is the real gesture.
		expect(r.open(nodes::shortcuts_version)).to_be_true();
		(void)r.ui.timerCallback(); // settle the first frame -- a render files nothing on its own
		expect(scene().take_action().engaged()).to_be_false();
		r.turn_select(-1);
		(void)r.ui.timerCallback();

		expect(static_cast<int>(soundEditor.shortcutsVersion)).to_equal(SHORTCUTS_VERSION_1);
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V1).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("the legacy -> port direction still works, with no menu and no dispatcher anywhere in it", _ {
		// **This is the control for the mutation, not a second copy of `boot_wiring_spec.cpp`.**
		// The two directions have to be in one file for "delete the dispatcher arm and the new
		// example reddens while this one does not" to be something a reader can re-run.
		//
		// `SoundEditor::setShortcutsVersion` is the sole writer of `shortcutsVersion` and mirrors
		// into this layer from there (`gui/ui/sound_editor.cpp:149-151`, F1 task 8's fix), which is
		// what makes a layout chosen from the legacy menu, at boot, or by a factory reset show up in
		// the ported tree. Nothing in this example touches a `Shell`, a `MenuScene` or the outbox.
		ShortcutsVersionGuard guard;

		// Both values, in both directions -- a mirror that only ever wrote V1 would pass a one-way
		// check.
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_1);
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V1).to_be_true();
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V3).to_be_true();
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_1);
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V1).to_be_true();
		// And the outbox stayed empty throughout: this direction files nothing, so the mirror call
		// inside `setShortcutsVersion` cannot be feeding the loop the enumerator's doc rules out.
		expect(scene().take_action().engaged()).to_be_false();
	});
});
// clang-format on

/// Seven `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead -- exactly as `context_spec.cpp` does.
///
/// `menu_action_open_perform`, `menu_action_stem_export` and `menu_action_three_verbs` are LAST on
/// purpose, and `UiStackGuard`'s note says why: they are the groups that put something on
/// `uiNavigationHierarchy`, and the one residue their teardown cannot undo is
/// `lastUIBeforeNullifying`. `menu_action_stem_export` is after `menu_action_open_perform` because it
/// is also the only group that opens the global guest; it reads neither `getCurrentUI()` nor that
/// residue, and `GlobalGuestGuard` returns the global to the depth 0 it was constructed at.
/// `menu_action_three_verbs` is after both because it is the only group that leaves a `SessionView`
/// mode moved -- `gridModeActive`, which is private and so cannot be put back from here (see
/// `MidiLearnGuard`).
///
/// `menu_action_perform_mirror` (F2 task 5) is last of all: it is the only group that reaches
/// `changeRootUI`, which runs a real root view's `opened()` and so leaves more behind than
/// `setRootUILowLevel` does, and the only one that moves shift and `AudioEngine::audioSampleTimer`.
/// `PerformGestureGuard` puts back everything it can name; being last is what covers what it cannot.
int menu_actions_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(menu_action_seam, menu_action_items, menu_action_perform, menu_action_dispatch,
	                          menu_action_open_perform, menu_action_stem_export, menu_action_three_verbs,
	                          menu_action_perform_mirror, menu_action_shortcuts_version)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
