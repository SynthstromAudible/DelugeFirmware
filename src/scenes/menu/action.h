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
/// | `perform_editor` | `performance_session_view::EditingMode::selectButtonPress` (`gui/menu_item/performance_session_view/editing_mode.h:50-97`) | writes `performanceView.defaultEditingMode`/`editingParam`, resets the performance view, `openUI(&performanceView)` |
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
/// ## The sixth item, which is a different shape (F2 task 6)
///
/// `shortcuts_version` is **not** one of those five and must not be read as one. Its select press
/// does nothing special: it is an ordinary `Kind::Enum` leaf, and legacy's own
/// `menu_item::shortcuts::Version` is an ordinary `Selection` the user opens and edits in place.
/// What crosses the wall is its **write**. `shortcuts::Version::writeCurrentValue`
/// (`gui/menu_item/shortcuts/version.h:27`) is `soundEditor.setShortcutsVersion(this->getValue())`,
/// and `SoundEditor::shortcutsVersion` is the byte `FlashStorage::writeSettings` stores at flash
/// byte 60 (`storage/flash_storage.cpp:982`). The ported hook could reach neither symbol, so it
/// wrote only this layer's mirror (`menu::set_shortcuts_version`, `scenes/menu/shortcuts.h`) and a
/// layout chosen from the ported settings menu was in force for the session and gone at the next
/// power-on. `MenuAction::SetShortcutsVersion` is that missing half.
///
/// Two consequences follow from "a write, not a press", and both are load-bearing below:
///
///   * it is filed from a `Binding::set` hook, which is handed no `MenuState`
///     (`SetFn`, `node.h`) -- hence `MenuScene::request_action`, whose own doc argues the timing;
///   * it must **not** ascend the menu, because legacy stays on the item while the user turns the
///     encoder. See the `switch` in `MenuScene::after_render` that decides that question.
///
/// ## The seventh, which is the sixth's shape again, twice over (F2 task 7)
///
/// `screensaver_mode` and `screensaver_timeout` are two more writes, not presses, and they share
/// **one** enumerator. Legacy's `ScreensaverModeMenu::writeCurrentValue` and
/// `ScreensaverTimeout::writeCurrentValue` (`gui/menu_item/defaults/screensaver.h:38-41`, `:60-63`)
/// are each two statements: a `FlashStorage` assignment, and
/// `hid::display::Screensaver::settingsChanged()`. The port already makes the assignment -- the two
/// hooks write `FlashStorage::screensaverMode` and `screensaverTimeoutMinutes` themselves
/// (`nodes/settings.cpp:600`, `:608`), so **these settings have always persisted**, which is the one
/// way this differs from `shortcuts_version`'s gap. What it could not do is the second statement:
/// `settingsChanged()` is `hid/`, which `src/scenes` may not include. So both screensavers kept
/// their old timings until the next boot or the next edit made from the legacy menu.
///
/// **One enumerator, because legacy's notify takes no argument.** `settingsChanged()` is a
/// zero-parameter static that re-reads both globals itself (`hid/display/screensaver.cpp:200-241`),
/// so there is nothing for a payload to carry and nothing for a second enumerator to distinguish:
/// the mode setter and the timeout setter ask for the identical thing. `arg` is unused.
///
/// ## The shape
///
/// The verb counterpart of the noun that already works:
///
///   1. `MenuAction` -- a closed enum of the actions a menu item may request.
///   2. `MenuActionRequest` -- that enumerator plus one `uint16_t` payload, for the two actions
///      whose meaning is not fixed by their identity (`OpenPerformanceView` carries which editing
///      mode; `SetShortcutsVersion` carries which layout).
///   3. `MenuState::pending_action` -- the field an item writes, exactly as it writes
///      `pending_scene` (`menu_scene.h`); or, for a hook that is handed no state,
///      `MenuScene::request_action`, which writes that same field.
///   4. `MenuScene::after_render` drains it **where `pending_scene` is drained**, and parks it in
///      `MenuScene::take_action()` for the application-side wiring to collect and perform.
///
/// ## What this file does NOT do
///
/// `take_action()` is an outbox and this file is only its producing half. Its consumer,
/// `gui::ui::dispatchShellActions` (`gui/ui/shell_action_dispatch.h`), arrived with F2 task 1 and
/// drains it once per guest tick; **all seven arms have bodies as of F2 task 7**. The specs in
/// `sim/scenes_spec/menu_actions_spec.cpp` assert both halves: that the right item emits the right
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
	/// The dispatcher owed `editing_mode.h:56-95` and pays it as of F2 task 2: set
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

	/// @brief `soundEditor.setShortcutsVersion(arg)` -- the legacy half of the ported
	///        `shortcuts_version` item's write. `arg` is the value the item was set to.
	///
	/// The dispatcher performs `shortcuts::Version::writeCurrentValue`'s one statement
	/// (`gui/menu_item/shortcuts/version.h:27`) on the real `soundEditor`, which F1 task 8
	/// established is the single writer covering all four legacy paths that change this setting
	/// (`SoundEditor::setShortcutsVersion`, `gui/ui/sound_editor.cpp:112-181`, the census in its own
	/// comment). That is what puts the choice in flash byte 60
	/// (`FlashStorage::writeSettings`, `storage/flash_storage.cpp:982`).
	///
	/// **`arg` is legacy's own unnormalised value, deliberately.** `writeCurrentValue` passes
	/// `this->getValue()` straight through and lets `setShortcutsVersion`'s `switch` decide what a
	/// value that is neither enumerator means ("Uses defaults", i.e. V3); the port passes the enum
	/// index straight through for the same reason, so the two cannot disagree about the mapping
	/// because neither of them applies one.
	///
	/// **This does not loop, and that is a design constraint rather than an observation.**
	/// `SoundEditor::setShortcutsVersion` mirrors *into* this layer -- it calls
	/// `menu::set_shortcuts_version` (`scenes/menu/shortcuts.h`), F1 task 8's fix. That callee is the
	/// free function that assigns one file-scope `ShortcutsVersion` in `menu/shortcuts.cpp`; it is
	/// **not** `nodes::set_shortcuts_version`, files no request and touches no `MenuState`. So the
	/// round trip is exactly one lap: hook -> request -> dispatcher -> legacy write -> mirror write,
	/// and it terminates there. Anything later that makes the *mirror* setter file a request would
	/// close that loop, which is why the mirror setter must stay a plain assignment.
	SetShortcutsVersion,

	/// @brief `hid::display::Screensaver::settingsChanged()` -- the notify half of **both** ported
	///        screensaver setters' writes. `arg` is unused.
	///
	/// The dispatcher performs the second statement of `ScreensaverModeMenu::writeCurrentValue` and
	/// of `ScreensaverTimeout::writeCurrentValue` (`gui/menu_item/defaults/screensaver.h:40`, `:62`).
	/// The first statement of each -- the `FlashStorage` assignment -- is already made by the ported
	/// hook itself, so unlike `SetShortcutsVersion` **no value is lost at power-off**; what was
	/// missing was only the immediate apply.
	///
	/// **One call updates both screensavers, which is why one enumerator is enough.**
	/// `Screensaver::settingsChanged()` (`hid/display/screensaver.cpp:200-241`) does three things:
	/// it pushes the recomputed idle period into the guest's own service
	/// (`shellHostUI.shell().set_screensaver_idle_ms(...)`, `:225-228`), it wakes the legacy
	/// screensaver if it is showing (`:230-233`), and it then either cancels the legacy timer for
	/// `ScreensaverMode::OFF` (`:235-238`) or re-arms it from now (`:240`, `arm()` at `:81-83`). So
	/// the legacy `hid::display::Screensaver` and the ported `shell::Screensaver` both move on the
	/// one call, and there is nothing for a second enumerator or a payload to say -- legacy's own
	/// notify is a zero-parameter static that re-reads both globals for itself.
	///
	/// **This does not loop.** `settingsChanged()` writes no menu state and files no request: its
	/// only reach back into this layer is `Shell::set_screensaver_idle_ms`, which sets one number on
	/// a `shell::Screensaver` service (`shell/shell.h:222-228`). Nothing on that path touches
	/// `MenuState`, so the round trip is hook -> request -> notify, and it stops.
	ScreensaverSettingsChanged,
};

/// @brief One request: which action, and the one payload word `OpenPerformanceView` and
///        `SetShortcutsVersion` need.
///
/// A `uint16_t` rather than a variant because the payload is one small number in both cases, and
/// because it is the same width `ui::shell::SceneId::arg` uses for the same job on the noun side.
struct MenuActionRequest {
	MenuAction action = MenuAction::None;
	uint16_t arg = 0;

	constexpr bool engaged() const { return action != MenuAction::None; }
	friend constexpr bool operator==(const MenuActionRequest&, const MenuActionRequest&) = default;
};

/// @name The performance view's editing mode, mirrored on this side of the wall.
///
/// `EditingMode::readCurrentValue` (`editing_mode.h:35-46`) reads `performanceView.defaultEditingMode`
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
/// **every** site that moves those two bools. There are **five writers: four inside
/// `PerformanceView`, plus legacy's own menu item.** The four are each a
/// `syncEditingModeMirror(*this)` immediately below the write they answer, in
/// `gui/views/performance_view.cpp`:
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
/// The fifth is `EditingMode::selectButtonPress` itself
/// (`gui/menu_item/performance_session_view/editing_mode.h:50-97`, the writes at `:56-57` and
/// `:60-61`, the call at `:84`) -- **the legacy menu item this node ports**, and the only writer outside the view.
///
/// **Why the fifth is not one of the four, and why excluding it was wrong.** "Behind the menu's
/// back" reads like it excludes the menu's own item, and F2 task 5's first pass excluded it on
/// exactly that reading. The exclusion holds only while `RuntimeFeatureSettingType::ShellMenu` is
/// **On**. With the toggle off, this item is the way into an editing mode from a menu
/// (`gui/views/performance_view.cpp:882`'s `else` branch), it writes both bools, and a mirror it
/// does not maintain sits stale until the toggle is flipped -- at which point `PerformEditorItem`
/// seeds its cursor from the rot (`menu/nodes/actions.cpp:121`). That is the same flip-on staleness
/// that makes gating the other four calls a bug, so the same answer applies: the mirror is
/// maintained by **every** writer, in both toggle states, or it is not maintained at all. The call
/// passes `currentMode` rather than re-deriving, because the branch above it set the bools *from*
/// that value; and it sits after the `if`/`else` chain so the `DISABLED` arm -- which returns at
/// `:52` without writing either bool -- does not reach it, matching what `PerformEditorItem` does.
///
/// **The writer set is now closed**, which is the claim that makes the rest true. Recipe, so it can
/// be re-checked rather than believed: `grep -rn "defaultEditingMode\|editingParam" src sim` and
/// keep the assignments. Nine are in `PerformanceView` (the four sites above, eight lines, plus the
/// constructor); four are `EditingMode::selectButtonPress`'s two arms; two pairs are
/// `gui/ui/shell_action_dispatch.cpp:94-99`, this seam's own arm, which runs *from* a mirror the
/// node has already set. Nothing else in the tree writes either bool. Every one of those either
/// syncs or is a pair that starts in agreement -- the constructor's `defaultEditingMode = false`
/// (`gui/views/performance_view.cpp:239`) against this mirror's own `DISABLED` initialiser
/// (`menu/nodes/actions.cpp:45`). That constructor write is deliberately **not** a sync site: it
/// runs at static init, the two already agree, and a call there would only add a dependency on two
/// translation units' initialisation order. The reasoning is repeated at that line, so a reader who
/// counts four calls against nine writes in that file does not "fix" it.
///
/// A site listed here until F2 task 5 **does not exist**: `focusRegained`
/// (`gui/views/performance_view.cpp:328-345`) writes neither bool. The write that entry's `:189`
/// pointed at was the constructor's, correctly numbered at the time and wrongly attributed.
///
/// With all five the mirror and the view agree at every point the menu can observe, **in both
/// toggle states**. This is the single node excluded from the generic port-vs-legacy value
/// comparison, for the same reason and with the same name as `shortcuts_version`.
///
/// All five calls are **ungated**, in legacy files, while `ShellMenu` is Off by default.
/// `syncEditingModeMirror`'s own comment carries the argument at length; the short form is that they
/// write one `static` inside `libdeluge_scenes`, read nothing, and cannot fail, so with the toggle
/// off each is a store to a byte no one loads. Both files are in `LEGACY_PORT_SEAMS`
/// (`scripts/check_layer_rules.py`), which is where that kind of decision is recorded.
///
/// @warning Nothing in this repository can check that list stays complete: a sixth writer, or a
///          fifth caller added to `releaseViewOnExit`, would desync the mirror silently -- no
///          checker here is content-aware enough to see either. The examples in
///          `sim/scenes_spec/menu_actions_spec.cpp`'s `menu_action_perform_mirror` group pin all
///          five sites and all four of that helper's callers, but they can only fail for a site
///          that exists; they cannot notice a new one. The durable fix is to move the two bools out
///          of the view, which is a larger legacy change than this task took on; until then this
///          comment is the contract.
/// @{

/// @brief What `EditingMode::readCurrentValue` would report.
PerformanceEditingMode performance_editing_mode();

/// @brief Set the mirror. Called by the ported item's own select press, by the four
///        `syncEditingModeMirror` sites in `gui/views/performance_view.cpp`, and by legacy's own
///        `EditingMode::selectButtonPress` (see above).
void set_performance_editing_mode(PerformanceEditingMode mode);

/// @}

} // namespace deluge::scenes::menu
