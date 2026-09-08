#pragma once

/// @file
/// @brief The **action seam**: how a menu item says "run this root-UI action" without the scenes
///        layer reaching into `gui/`.
///
/// `MenuState::pending_scene` is the noun half of this seam and has been there since task 5: an item
/// names a *scene* and `MenuScene::after_render` turns the request into a `Push`. It works because
/// every scene a menu wants to open is inside `src/scenes`. Five ported items are not like that --
/// what their select press does is run a piece of legacy root-UI behaviour:
///
// clang-format off
/// | node | legacy | what its select press does |
/// |---|---|---|
/// | `perform_editor` | `performance_session_view::EditingMode::selectButtonPress` (`gui/menu_item/performance_session_view/editing_mode.h:49-74`) | writes `performanceView.defaultEditingMode`/`editingParam`, resets the performance view, `openUI(&performanceView)` |
/// | `start_stem_export` | `stem_export::Start::selectButtonPress` (`gui/menu_item/stem_export/start.h:31-49`) | `soundEditor.exitCompletely()`, then `getRootUI()` chooses one of four `StemExportType`s |
/// | `set_clip_length` | `audio_clip::SetClipLengthEqualToSampleLength::selectButtonPress` (`gui/menu_item/audio_clip/set_clip_length_equal_to_sample_length.h:26-29`) | `audioClipView.setClipLengthEqualToSampleLength()` |
/// | `song_midi_learn` | `song::MidiLearn::selectButtonPress` (`gui/menu_item/song/midi_learn.h:27-31`) | `context_menu::midiLearnMode.setupAndCheckAvailability()` + `openUI(...)` |
/// | `configure_song_macros` | `song::ConfigureMacros::selectButtonPress` (`gui/menu_item/song/configure_macros.h:27-31`) | `context_menu::configureSongMacros.setupAndCheckAvailability()` + `openUI(...)` |
// clang-format on
///
/// Every one of those five statements names a symbol in `gui/views/`, `gui/ui/` or
/// `gui/context_menu/`. `src/scenes` may not include any of them (`scenes-includes-outside`,
/// `scripts/check_layer_rules.py`), and that is not an accident of the checker -- it is the whole
/// reason this seam exists. Two of the five (`song_midi_learn`, `configure_song_macros`) open
/// legacy `ContextMenu` subclasses that this plan has not ported, so even `pending_scene` could not
/// spell them.
///
/// ## The shape
///
/// The verb counterpart of the noun that already works:
///
///   1. `MenuAction` -- a closed enum of the actions a menu item may request.
///   2. `MenuActionRequest` -- that enumerator plus one `uint16_t` payload, for the one action whose
///      meaning is not fixed by its identity (`OpenPerformanceView` carries which editing mode).
///   3. `MenuState::pending_action` -- the field an item writes, exactly as it writes
///      `pending_scene` (`menu_scene.h`).
///   4. `MenuScene::after_render` drains it **where `pending_scene` is drained**, and parks it in
///      `MenuScene::take_action()` for the application-side wiring to collect and perform.
///
/// ## What this file does NOT do
///
/// `take_action()` is an outbox and this file is only its producing half. Its consumer,
/// `gui::ui::dispatchShellActions` (`gui/ui/shell_action_dispatch.h`), arrived with F2 task 1 and
/// drains it once per guest tick; **all five arms have bodies as of F2 task 4**. The specs in
/// `sim/scenes_spec/menu_actions_spec.cpp` assert both halves: that the right item emits the right
/// *identity* and payload through the real input path, and -- in the three `dispatchShellActions`
/// groups -- that the legacy effect happens. Only `StartStemExport`'s acting half is still pinned by
/// identity alone, for a reason that file's own group states.
///
/// @note **Legacy's own post-conditions travel with the intent, not with the menu.** Two of the
///       five have a documented effect on the menu itself -- `stem_export::Start` calls
///       `soundEditor.exitCompletely()` before doing anything, and `EditingMode` returns
///       `NO_NAVIGATION` rather than ascending. The port reproduces what it can from inside the
///       scene (see `nodes/actions.h`) and leaves the rest to the dispatcher; each divergence is
///       named there rather than silently absorbed.

#include "definitions_cxx.hpp" // PerformanceEditingMode

#include <cstdint>

namespace deluge::scenes::menu {

/// @brief The closed set of root-UI actions a menu item may request.
///
/// Closed on purpose: a wiring-side dispatcher can `switch` over it and a new enumerator breaks that
/// switch loudly. `None` is the "no request" state of `MenuState::pending_action`, the way
/// `SceneKind::Null` is `pending_scene`'s.
enum class MenuAction : uint8_t {
	/// @brief No action requested.
	None = 0,

	/// @brief Enter the performance view in the editing mode named by `MenuActionRequest::arg`
	///        (a `PerformanceEditingMode`, and never `DISABLED` -- see `nodes/actions.h`).
	///
	/// The dispatcher owed `editing_mode.h:55-72` and pays it as of F2 task 2: set
	/// `performanceView.defaultEditingMode = true` and `editingParam` from the payload, call
	/// `performanceView.resetPerformanceView(modelStack)` unless `editingParam`, then `openUI(...)`.
	OpenPerformanceView,

	/// @brief Begin a stem export. `arg` is unused.
	///
	/// The dispatcher owes all of `stem_export/start.h:31-49`, **including the `getRootUI()` fan-out
	/// that picks between `StemExportType::MIXDOWN`/`TRACK`/`CLIP`/`DRUM` and the
	/// `soundEditor.exitCompletely()` that precedes it**. Deliberately not four enumerators: the
	/// choice is made from the root UI, which this layer cannot read, so splitting it here would
	/// only move an unanswerable question into the menu.
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
/// A `uint16_t` rather than a variant because exactly one action carries anything, and because it is
/// the same width `ui::shell::SceneId::arg` uses for the same job on the noun side.
struct MenuActionRequest {
	MenuAction action = MenuAction::None;
	uint16_t arg = 0;

	constexpr bool engaged() const { return action != MenuAction::None; }
	friend constexpr bool operator==(const MenuActionRequest&, const MenuActionRequest&) = default;
};

/// @name The performance view's editing mode, mirrored on this side of the wall.
///
/// `EditingMode::readCurrentValue` (`editing_mode.h:34-45`) reads `performanceView.defaultEditingMode`
/// and `performanceView.editingParam` -- two plain `bool` members of a `gui/views/` class
/// (`gui/views/performance_view.h:112-113`), read and written from thirty-odd places inside that
/// view. There is no bare-`extern` escape from a class member (the trick `settings.cpp` uses for
/// `PadLEDs::flashCursor`), and unlike task 11's `Colour::value` these are not one setting byte that
/// obviously belongs in `FlashStorage` -- they are that view's live mode. So the port owns a mirror,
/// which is exactly the arrangement `menu::shortcuts_version()` already documents for
/// `soundEditor.shortcutsVersion` (`scenes/menu/shortcuts.h`, task 11 §4).
///
/// **What the wiring plan owed, and what it paid (F2 task 5, shipped).** The mirror is
/// authoritative for what the menu *shows* and for what `MenuAction::OpenPerformanceView` carries.
/// It is not authoritative for the view, so `set_performance_editing_mode()` has to be called at
/// **every** site where the view moves those two bools behind the menu's back. There are **four**,
/// they are all in `gui/views/performance_view.cpp`, and each one is now a
/// `syncEditingModeMirror(*this)` immediately below the write it answers:
///
///   1. `PerformanceView::releaseViewOnExit` (`:1402-1412`, the writes at `:1405-1406`, the call at
///      `:1411`). **This is the ordinary exit path**, and the one that matters most and is easiest
///      to miss, because it is a private helper rather than a button arm. **Four callers reach it,
///      not three**: `buttonAction`'s CLIP_VIEW arm (`:748`), its KEYBOARD-press exit (`:959`) and
///      its KEYBOARD-release exit (`:977`), and `padAction`'s grid-mode pad (`:1092`).
///   2. `buttonAction`'s BACK arm (`:838-848`, the writes at `:840-841`, the call at `:844`).
///   3. `buttonAction`'s shift+KEYBOARD exit (`:924-932`, the writes at `:925-926`, the call at
///      `:928`).
///   4. `buttonAction`'s shift+KEYBOARD *cycle* (`:933-956`, the writes at `:936` and `:938`, the
///      call at `:942`). The only one of the four whose answer is not fixed: it produces `VALUE`
///      when the press entered editing mode and `PARAM` when it cycled within it, which is why the
///      call sits after **both** writes.
///
/// A fifth site was listed here until F2 task 5 and **does not exist**: `focusRegained` (`:328-345`)
/// writes neither bool. The `defaultEditingMode = false` that entry pointed at is in
/// `PerformanceView::PerformanceView()` (`:239`), the constructor, and it is deliberately **not** a
/// sync site -- it runs at static init and the mirror's own initialiser is already `DISABLED`
/// (`menu/nodes/actions.cpp:45`), so the two agree and a call there would only add a dependency on
/// two translation units' initialisation order. The reasoning is repeated at that line, so a reader
/// who counts four calls against five writes does not "fix" it.
///
/// With all four the mirror and the view agree at every point the menu can observe. This is the
/// single node excluded from the generic port-vs-legacy value comparison, for the same reason and
/// with the same name as `shortcuts_version`.
///
/// The four calls are **ungated**, in a legacy file, while `RuntimeFeatureSettingType::ShellMenu` is
/// Off by default. `syncEditingModeMirror`'s own comment carries the argument for that; the short
/// form is that they write one `static` inside `libdeluge_scenes` and nothing loads it with the
/// toggle off. `gui/views/performance_view.cpp` is in `LEGACY_PORT_SEAMS`
/// (`scripts/check_layer_rules.py`) for it, which is where that decision is recorded.
///
/// @warning Nothing in this repository can check that list stays complete: a fifth write added to
///          `PerformanceView`, or a fifth caller added to `releaseViewOnExit`, would desync the
///          mirror silently -- no checker here is content-aware enough to see either. The examples
///          in `sim/scenes_spec/menu_actions_spec.cpp`'s `menu_action_perform_mirror` group pin the
///          four sites and all four of that helper's callers, one example each, but they can only
///          fail for a site that exists; they cannot notice a new one. The durable fix is to move
///          the two bools out of the view, which is a larger legacy change than this task took on;
///          until then this comment is the contract.
/// @{

/// @brief What `EditingMode::readCurrentValue` would report.
PerformanceEditingMode performance_editing_mode();

/// @brief Set the mirror. Called by the item's own select press, and by the four
///        `syncEditingModeMirror` sites in `gui/views/performance_view.cpp` (see above).
void set_performance_editing_mode(PerformanceEditingMode mode);

/// @}

} // namespace deluge::scenes::menu
