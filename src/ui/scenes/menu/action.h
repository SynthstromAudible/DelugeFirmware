#pragma once

/// @file
/// @brief The **action seam**: how a menu item says "run this root-UI action" without the scenes
///        layer reaching into `gui/`.
///
/// `MenuWalkState::pending_scene` is the noun half of this seam: an item
/// names a *scene* and `MenuScene::after_render` turns the request into a `Push`. It works because
/// every scene a menu wants to open is inside `src/ui/scenes`. Five menu items are not like that --
/// what their select press does is run a piece of legacy root-UI behaviour:
///
// clang-format off
/// | node | legacy | what its select press does |
/// |---|---|---|
/// | `perform_editor` | `performance_session_view::EditingMode::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`) | writes `performanceView.defaultEditingMode`/`editingParam`, resets the performance view, `openUI(&performanceView)` |
/// | `start_stem_export` | `stem_export::Start::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`) | `soundEditor.exitCompletely()`, then `getRootUI()` chooses one of four `StemExportType`s |
/// | `set_clip_length` | `audio_clip::SetClipLengthEqualToSampleLength::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/audio_clip/set_clip_length_equal_to_sample_length.h`) | `audioClipView.setClipLengthEqualToSampleLength()` |
/// | `song_midi_learn` | `song::MidiLearn::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/song/midi_learn.h`) | `context_menu::midiLearnMode.setupAndCheckAvailability()` + `openUI(...)` |
/// | `configure_song_macros` | `song::ConfigureMacros::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/song/configure_macros.h`) | `context_menu::configureSongMacros.setupAndCheckAvailability()` + `openUI(...)` |
// clang-format on
///
/// Every one of those five statements names a symbol in `gui/views/`, `gui/ui/` or
/// `gui/context_menu/`. `src/ui/scenes` may not include any of them (`scenes-includes-outside`,
/// `scripts/check_layer_rules.py`), and that is not an accident of the checker -- it is the whole
/// reason this seam exists. Two of the five (`song_midi_learn`, `configure_song_macros`) open
/// legacy `ContextMenu` subclasses the port does not have, so even `pending_scene` could not
/// spell them.
///
/// ## Settings writes are not actions
///
/// A settings item's write that has a side effect this layer may not perform -- the screensaver's
/// `Screensaver::settingsChanged()`, the pad brightness, the menu highlighting, rounded corners,
/// sticky shift, leaving the slow cursor flash, a pad colour -- calls the host synchronously from
/// its `Binding::set` hook through a port (`ui/scenes/bind/ports/`), as main's `writeCurrentValue` makes both
/// its statements at once. So does the file selector's end of a keyboard audition.
///
/// ## The shape
///
/// The verb counterpart of the noun that already works:
///
///   1. `MenuAction` -- a closed enum of the actions a menu item may request.
///   2. `MenuActionRequest` -- that enumerator plus one `uint16_t` payload, for the action
///      whose meaning is not fixed by its identity (`OpenPerformanceView` carries which editing
///      mode).
///   3. `MenuWalkState::pending_action` -- the field an item writes from `on_focus` or `render`,
///      exactly as it writes `pending_scene` (`menu_scene.h`).
///   4. `MenuScene::after_render` drains it **where `pending_scene` is drained**, and parks it in
///      `MenuScene::take_action()` for the application-side wiring to collect and perform.
///
/// ## What this file does NOT do
///
/// `take_action()` is an outbox and this file is only its producing half. Its consumer,
/// `gui::ui::dispatchShellActions` (`gui/ui/shell_action_dispatch.h`), drains it once per guest tick;
/// **every arm has a body**. The specs in
/// `tests/integration/ui/scenes/menu_actions_spec.cpp` assert both halves: that the right item emits the right
/// *identity* and payload through the real input path, and -- in the four `dispatchShellActions`
/// groups -- that the legacy effect happens. Only `StartStemExport`'s acting half is still pinned by
/// identity alone, for a reason that file's own group states.
///
/// @note **Legacy's own post-conditions travel with the intent, not with the menu.** Two of the
///       five have a documented effect on the menu itself -- `stem_export::Start` calls
///       `soundEditor.exitCompletely()` before doing anything, and `EditingMode` returns
///       `NO_NAVIGATION` rather than ascending. The port reproduces what it can from inside the
///       scene (see `nodes/actions.h`) and leaves the rest to the dispatcher; each divergence is
///       named there rather than silently absorbed.

#include <cstdint>

namespace deluge::ui::scenes::menu {

/// @brief The closed set of root-UI actions a menu item may request.
///
/// Closed on purpose: a wiring-side dispatcher can `switch` over it and a new enumerator breaks that
/// switch loudly. `None` is the "no request" state of `MenuWalkState::pending_action`, the way
/// `SceneKind::Null` is `pending_scene`'s.
enum class MenuAction : uint8_t {
	/// @brief No action requested.
	None = 0,

	/// @brief Enter the performance view in the editing mode named by `MenuActionRequest::arg`
	///        (a `bind::PerformanceEditing`, and never `Disabled` -- see `nodes/actions.h`).
	///
	/// The dispatcher performs `EditingMode::selectButtonPress`
	/// (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`): set
	/// `performanceView.defaultEditingMode = true` and `editingParam` from the payload, call
	/// `performanceView.resetPerformanceView(modelStack)` unless `editingParam`, then `openUI(...)`.
	OpenPerformanceView,

	/// @brief Begin a stem export. `arg` is unused.
	///
	/// The dispatcher performs all of `Start::selectButtonPress`
	/// (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`), **including the `getRootUI()` fan-out that picks
	/// between `StemExportType::MIXDOWN`/`TRACK`/`CLIP`/`DRUM`**. The `soundEditor.exitCompletely()` that precedes it
	/// (the first statement of `Start::selectButtonPress`) is performed *in equivalent form rather than verbatim*, and
	/// that is the one divergence: under the guest the open UI is `ShellHostUI` and not `soundEditor`, so the literal
	/// call would walk `closeUI` off the front of the navigation stack. `closePortedMenu`
	/// (`gui/ui/shell_action_dispatch.cpp`) empties the guest's scene stack instead and lets
	/// `ShellHostUI::timerCallback` do the close it already owns -- the same statement the menu's own root-Back
	/// reaches. That file argues it at length; this reference exists so the divergence is not discoverable only from
	/// there.
	///
	/// Deliberately not four enumerators: the choice is made from the root UI, which this layer
	/// cannot read, so splitting it here would only move an unanswerable question into the menu.
	StartStemExport,

	/// @brief `audioClipView.setClipLengthEqualToSampleLength()`. `arg` is unused.
	SetClipLengthToSampleLength,

	/// @brief Open the song MIDI-learn context menu (`context_menu::midiLearnMode`). `arg` is unused.
	OpenSongMidiLearn,

	/// @brief Open the song-macros context menu (`context_menu::configureSongMacros`). `arg` is
	///        unused.
	OpenSongMacros,
};

/// @brief One request: which action, and the one payload word `OpenPerformanceView` needs.
///
/// A `uint16_t` rather than a variant because the payload is one small number, and
/// because it is the same width `ui::shell::SceneId::arg` uses for the same job on the noun side.
struct MenuActionRequest {
	MenuAction action = MenuAction::None;
	uint16_t arg = 0;

	constexpr bool engaged() const { return action != MenuAction::None; }
	friend constexpr bool operator==(const MenuActionRequest&, const MenuActionRequest&) = default;
};

} // namespace deluge::ui::scenes::menu
