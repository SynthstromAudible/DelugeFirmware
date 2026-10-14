#pragma once

/// @file
/// @brief The ported menu, opened at a chosen root on the GLOBAL guest's own scene stack, and the
///        two guards that put the guest and legacy's UI hierarchy back afterwards.
///
/// Shared by `note_mode_spec.cpp` and `pad_forward_spec.cpp` (note-family Task 7), which both ask
/// `gui::ui::activeNoteMode()`. That function reads `shellHostUI`'s real `SceneStack`, so the menu
/// has to be on the singleton guest's stack -- a spec-local `ShellHostUI` would not be seen.

#include "gui/ui/shell_host_ui.h" // shellHostUI
#include "gui/ui/ui.h"            // nullifyUIs, setRootUILowLevel, openUI
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include "song_fixture.h"

namespace deluge::scenes::spec {

/// @brief Put the legacy UI hierarchy back to "nothing open" after an example seats a root with
///        `setRootUILowLevel` or opens the guest with `openUI`.
///
/// `menu_actions_spec.cpp`'s `UiStackGuard`, unchanged: `nullifyUIs()` is what legacy itself calls
/// to tear the hierarchy down (`gui/ui/ui.cpp:257-261`), and `closeUI` is not safe here for the
/// same `[-1]` reason that file's copy of this note gives.
struct UiStackGuard {
	UiStackGuard(const UiStackGuard&) = delete;
	UiStackGuard& operator=(const UiStackGuard&) = delete;
	UiStackGuard() = default;
	~UiStackGuard() { nullifyUIs(); }
};

/// @brief Empty the GLOBAL guest's scene stack and unset `TimerName::UI_SPECIFIC`, whichever way the
///        example ends.
///
/// `shellHostUI` is a process singleton, so a menu an example leaves on its stack would still be
/// there for the next example -- and `activeNoteMode()` would still see it. The timer unset is
/// `menu_actions_spec.cpp`'s `UiSpecificTimerGuard`: `ShellHostUI::opened()` arms it.
struct GlobalGuestGuard {
	GlobalGuestGuard(const GlobalGuestGuard&) = delete;
	GlobalGuestGuard& operator=(const GlobalGuestGuard&) = delete;
	GlobalGuestGuard() = default;
	~GlobalGuestGuard() {
		empty_guest_stack();
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	}
	/// Pop every scene off `shellHostUI`'s stack, for an example that re-opens mid-way.
	static void empty_guest_stack() {
		while (shellHostUI.shell().stack().depth() > 0) {
			shellHostUI.shell().stack().pop(deluge::ui::shell::SceneKind::Null);
		}
	}
};

/// @brief The node a root opens at: the two note roots' own, and `root_song` for every other root.
///
/// `root_song` stands in for the rest because `activeNoteMode` and `exitNoteModeBeforeClose` read the
/// context's root and nothing about the node, and `root_song`'s items need nothing but a `Song` --
/// unlike, say, `Root::Drum`'s, which would need a drum a fixture may not build.
inline const menu::Node& menu_node_for_root(menu::MenuContext::Root root) {
	switch (root) {
	case menu::MenuContext::Root::NoteEditor:
		return menu::nodes::note_editor_root;
	case menu::MenuContext::Root::NoteRowEditor:
		return menu::nodes::note_row_editor_root;
	default:
		return menu::nodes::root_song;
	}
}

/// @brief The context `openShellMenu`'s `NoteMode` installs -- `context_for` over the fixture's song
///        and clip, then `ctx.root` set (`gui/ui/shell_menu_entry.cpp:136-139`) -- for any root.
inline menu::MenuContext menu_context_at(SongFixture& fx, menu::MenuContext::Root root) {
	menu::MenuContext ctx = menu::context_for(*fx.song, fx.clip, 0, /*performance_view=*/false,
	                                          /*session_or_arranger_root=*/false, /*session_view_root=*/false,
	                                          /*kit_global_fx=*/false);
	ctx.root = root;
	return ctx;
}

/// @brief Open the real `MenuScene` at @p root as the only scene on the global guest's stack.
///
/// **What this establishes, exactly:**
///   * `menu_scene()` is open at `menu_node_for_root(root)` with `menu_context_at(fx, root)`, so
///     `menu_scene().menu_context().root == root`. That is the context `openShellMenu`'s `NoteMode`
///     installs, **without** that entry point's side effects: no kit `affectEntire` clear, no undo-log
///     rule, no `view.setActiveModControllableTimelineCounter`.
///   * `menu::assign_node_ids` has run over that one root alone, replacing any earlier assignment.
///   * `shellHostUI`'s initial scene is `{FirstApp, node_id(node)}` and `shellHostUI.opened()` has
///     run: the guest's stack holds exactly the menu, because `opened()` starts a fresh stack. It also
///     re-wires the three guest services and arms `TimerName::UI_SPECIFIC` -- hence
///     `GlobalGuestGuard`. So `gui::ui::activeNoteMode()` answers the root's mode.
///
/// **What it does not establish:** the guest on `uiNavigationHierarchy` (no `openUI`, so
/// `getCurrentUI()` is whatever it was), a root UI (seat one with `setRootUILowLevel`, torn down by
/// `UiStackGuard`), `currentUIMode`, or any recorder (`gui::ui::setNoteModeLegacyCalls`,
/// `gui::ui::setPadForwardPerform`) -- the real legacy calls stay installed unless an example
/// replaces them.
///
/// @return What `opened()` returned: false only if the guest refused to open.
inline bool open_menu_on_global_guest_at(SongFixture& fx, menu::MenuContext::Root root) {
	const menu::Node& node = menu_node_for_root(root);
	const menu::Node* const roots[] = {&node};
	(void)menu::assign_node_ids(roots);
	menu_scene().open(node, menu_context_at(fx, root));
	shellHostUI.setInitialScene(
	    deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(node)});
	return shellHostUI.opened();
}

/// @brief The ported menu open at @p root in production's shape: `instrumentClipView` as the root UI
///        and the guest opened over it as the current UI.
///
/// **What this establishes, beyond `open_menu_on_global_guest_at`:** `open_menu_on_global_guest_at`'s
/// menu, context and node ids, and then the guest on `uiNavigationHierarchy` -- `setRootUILowLevel(
/// &instrumentClipView)` followed by `openUI(&shellHostUI)`, which runs `opened()` itself and so starts
/// the guest's stack at the initial scene. `getCurrentUI()` is `&shellHostUI` and `getRootUI()` is
/// `&instrumentClipView`, exactly as when `openShellMenu` opens a note editor from the clip view.
///
/// **Why the guest must be current over `instrumentClipView`.** `ActionLogger::getNewAction` refuses
/// an action whenever the current UI is not the root, unless a note editor is open
/// (`model/action/action_logger.cpp:98-102`) -- the gate every note-mode edit is written through. With no
/// UI seated, `getRootUI()` is null and `getCurrentUI()` is whatever `nullifyUIs` last left
/// (`gui/ui/ui.cpp:136-150`), so the gate's answer would be an accident of the previous example rather
/// than the question production asks. And `instrumentClipView` is the root every note-mode handler
/// runs under. Tear down with `UiStackGuard` and `GlobalGuestGuard`.
///
/// @return What `openUI` returned: false only if the guest refused to open.
inline bool open_note_mode_over_clip_view(SongFixture& fx, menu::MenuContext::Root root) {
	setRootUILowLevel(&instrumentClipView);
	const menu::Node& node = menu_node_for_root(root);
	const menu::Node* const roots[] = {&node};
	(void)menu::assign_node_ids(roots);
	menu_scene().open(node, menu_context_at(fx, root));
	shellHostUI.setInitialScene(
	    deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(node)});
	return openUI(&shellHostUI);
}

} // namespace deluge::scenes::spec
