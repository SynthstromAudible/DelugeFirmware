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
///     the `openUI`. It is the only verb with a body.
///   * **NOT covered:** that the other four verbs do anything. Their arms are still empty, so
///     nothing here asserts that a stem export starts or that a clip's length changes.
///
/// Legacy is the oracle wherever it can be reached: `performEditorMenu`'s real option list and its
/// real `readCurrentValue` mapping are both run here, on the real `performanceView`, and compared
/// against the port. The four pure verbs have no comparable surface -- their legacy bodies are one
/// `openUI`/`startStemExportProcess`/`setClipLengthEqualToSampleLength` statement each, which is
/// exactly the half this layer does not own -- so they are pinned by identity instead, the same way
/// task 6's five reset descriptors are.

#include "scenes/l10n_bridge.h"
#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/actions.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/performance_session_view/editing_mode.h"
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h" // isUIOpen / nullifyUIs / setRootUILowLevel, for the groups that use the real UI stack
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "model/song/song.h"
#include "processing/stem_export/stem_export.h"

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
#include <vector>

#include "spec/support/spec_main.h"

/// The real `performance_session_view::EditingMode` singleton. Declared bare at global scope
/// because `gui/ui/menus.cpp:2005` defines it there (that file's `using namespace` lines are what
/// make its `menu_item::` spelling work) and `gui/ui/menus.h` does not export it -- the same
/// situation `tables_3_spec.cpp:589` records for `flashStatusMenu` and friends.
extern deluge::gui::menu_item::performance_session_view::EditingMode performEditorMenu;

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
constexpr const Node* kRoots[] = {&nodes::root_song, &nodes::root_audio_clip, &nodes::root_performance};

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
///        lowering, the real `list` widget and the real `enter_child`.
/// @return The action the scene filed as a result, drained exactly once.
MenuActionRequest press_child(Rig& r, const Node& parent, const Node& child) {
	const MenuContext ctx = r.open(parent);
	const int32_t index = visible_index_of(parent, child, ctx);
	if (index < 0) {
		return MenuActionRequest{};
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
	return scene().take_action();
}

/// @brief The port's transcription of `EditingMode::readCurrentValue` (`editing_mode.h:34-45`),
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
	// (`gui/views/performance_view.cpp:1306-1307`), which is `initPadPress(firstPadPress)` +
	// `initPadPress(lastPadPress)` (`:1352-1355`), and `initPadPress` clears `isActive` (`:223-229`).
	// `lastPadPress` is the half of that pair the header makes public (`performance_view.h:119`).
	//
	// Nothing else on the dispatcher's path writes it: the only other writers in the whole view are
	// `padPressAction` (`:1124`, the writes at `:1141-1145`), `padReleaseAction`'s `initPadPress`
	// (`:1149`, the call at `:1171`) and `restorePreviousHoldPress` (`:1259-1262`), none of which
	// `openUI` -> `opened()` -> `focusRegained()` (`:269-296`) can reach. So `isActive` going false
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
	// Nothing in THIS group asserts that anything is performed -- it is about the drain. The one arm
	// that performs anything, `OpenPerformanceView`, has its own group below; the other four are
	// still empty and each arrives with its own task.

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

		// `editing_mode.h:54-57` -- the VALUE branch.
		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_false();
		// `editing_mode.h:63-70` -- `!editingParam`, so the reset runs.
		expect(performanceView.lastPadPress.isActive).to_be_false();
		// `editing_mode.h:72` -- `openUI(&performanceView)`.
		expect(isUIOpen(&performanceView)).to_be_true();
		// And the outbox is empty, so the effects above came from this one request rather than from
		// a second delivery of it.
		expect(scene().take_action().engaged()).to_be_false();
	});

	it("PARAM: writes both bools and opens the view, and does NOT reset it", _ {
		// Legacy's own reason, at `editing_mode.h:64-65`: resetting in param-editing mode "would
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

		// `editing_mode.h:58-61` -- the PARAM branch.
		expect(performanceView.defaultEditingMode).to_be_true();
		expect(performanceView.editingParam).to_be_true();
		// `editing_mode.h:63` -- `editingParam`, so the reset is SKIPPED and the marker stands.
		expect(performanceView.lastPadPress.isActive).to_be_true();
		// `editing_mode.h:72` -- the open happens either way; only the reset is conditional.
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
	// The second arm with a body (F2 task 3), and the one `scenes/menu/action.h:85-87` deliberately
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
		// (`scenes/menu/menu_scene.cpp:1447-1452`; `gui/ui/sound_editor.cpp:710-717`, the call at
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
// clang-format on

/// Five `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead -- exactly as `context_spec.cpp` does.
///
/// `menu_action_open_perform` and `menu_action_stem_export` are LAST on purpose, and `UiStackGuard`'s
/// note says why: they are the two groups that put something on `uiNavigationHierarchy`, and the one
/// residue their teardown cannot undo is `lastUIBeforeNullifying`. `menu_action_stem_export` is
/// after `menu_action_open_perform` because it is also the only group that opens the global guest;
/// it reads neither `getCurrentUI()` nor that residue, and `GlobalGuestGuard` returns the global to
/// the depth 0 it was constructed at.
int menu_actions_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(menu_action_seam, menu_action_items, menu_action_perform, menu_action_dispatch,
	                          menu_action_open_perform, menu_action_stem_export)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
