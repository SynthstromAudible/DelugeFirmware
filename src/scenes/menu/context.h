#pragma once

/// @brief `MenuContext` -- what a menu edits -- and the three pure functions that produce it, reach
///        the model through it, and gate entry into a node with it.
///
/// This is the context-resolution half of `SoundEditor::setup`, `SoundEditor::getCurrentModelStack` and the common rule
/// behind the `checkPermissionToBeginSession` overrides
/// (`SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`), ported as free functions over plain arguments.
/// (`gui/menu_item/` has 18 *files* naming `checkPermissionToBeginSession`; two of them,
/// `origin/main:src/deluge/gui/menu_item/menu_item.cpp`/`.h`, are the base rather than overrides.)
///
/// @note **No UI reads.** `setup()` consults `getCurrentUI()`, `currentUIMode`,
///       `rootUIIsClipMinderScreen()`, `Buttons::isShiftButtonPressed()` and its own
///       `setupKitGlobalFXMenu` member. None of that is reachable from `src/scenes` -- the caller
///       reduces it to `context_for`'s `performance_view` / `session_or_arranger_root` /
///       `session_view_root` / `kit_global_fx` arguments. See `context.cpp` for the branch-by-branch
///       mapping and for what stayed behind.

#include "midi/device_id.h"
#include "scenes/bind/ports/ports.h"
#include "scenes/bind/target.h"
#include "scenes/services/view_state.h"

#include <cstdint>
#include <vector>

class Song;

namespace deluge::project {
class Song;
} // namespace deluge::project

namespace deluge::scenes::menu {

struct Node;

/// @brief Everything a menu item needs to know about what it is editing.
///
/// One value replaces the twelve `soundEditor.current*` members: a menu
/// hook is handed a `const MenuContext&` instead of reading a global.
///
/// It holds what a node edits by address -- the record (`model`), the owners (`addresses`) -- and what it reads and
/// writes through: the host's ports. Its flags say which kind of context `context_for()`
/// resolved. The engine objects it was built from stay in the builder.
struct MenuContext {
	/// @brief The `HorizontalMenus` community feature, read when the context is made: off, every horizontal page
	///        renders and edits as a vertical list and the horizontal-menu chains answer nothing, as on `main`
	///        (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`'s `HorizontalMenu::renderingStyle` and
	///        `origin/main:src/deluge/gui/ui/sound_editor.cpp`'s `SoundEditor::getCurrentHorizontalMenusChain` on
	///        `origin/main`).
	bool horizontal_menus = true;
	/// @brief The SHIFT shortcut overlay setting is on (`RuntimeFeatureSettingType::ShortcutOverlay` not `Off`; on
	///        by default, as `MenuOnly` is).
	bool shortcut_overlay = true;

	/// @brief Which root menu `setup()` would have opened (`origin/main:src/deluge/gui/ui/sound_editor.cpp`).
	///
	/// `Count` is the enumerator count, not a root.
	///
	/// @note `setup()` picks `NoteEditor`/`NoteRowEditor` from a `currentUIMode` this layer may not
	///       read (`noteEditorRootMenu` and `noteRowEditorRootMenu` in `SoundEditor::setup`) -- so, unlike every
	///       other enumerator here, `context_for` never assigns either one; a caller that wants one
	///       (`gui/ui/menu_entry.cpp`'s `openMenu`, `NoteMode`) sets `ctx.root` itself
	///       after resolving the rest of the context.
	enum class Root : uint8_t {
		Sound,
		Drum,
		MidiOrCv,
		MidiDrum,
		GateDrum,
		AudioClip,
		Performance,
		Song,
		KitGlobalFx,
		Settings,
		NoteEditor,
		NoteRowEditor,
		Count,
	};

	/// @brief The song, for the device and session menus that act on the running song itself (its threshold
	///        recording mode, its stem export, the velocity default every instrument takes). Everything else a node
	///        edits it reaches through `model`, `addresses` and `ports`.
	Song* song = nullptr;
	/// @brief The range the menu edits, by index into the source's ranges; -1 for none chosen.
	int16_t range_index = -1;
	/// @brief The sound source the menu edits.
	int8_t source_index = 0;

	/// @brief `soundEditor`'s current MIDI cable, by its handle. `setup()` never assigns it (the MIDI-device menus do,
	///        from their own selection), so `context_for()` always leaves it naming none.
	deluge::midi::DeviceId midi_device{};

	/// @brief Is the AFFECT-ENTIRE button held right now? Legacy's
	///        `currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR`
	///        (`UnpatchedParam::writeCurrentValue` and the 20-odd items that spell the same test).
	///
	/// A UI fact, so -- like `midi_device` -- `context_for()` never assigns it: the caller sets it,
	/// exactly as it reduces the other UI reads to `context_for`'s `bool` arguments.
	/// `bindings::write()` is its only reader; with it set on a kit row, a param write goes to every
	/// `SoundDrum` in the kit instead of just the selected one.
	///
	/// @note NOT the same fact as `affect_entire` below. That one is the clip's own `InstrumentClip::affectEntire`
	///       setting, which is true on the kit-row grid-shortcut path
	///       (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) where legacy still edits one row only; the button being
	///       held is independent of it (the `AFFECT_ENTIRE` arm of `SoundEditor::buttonAction` in
	///       `origin/main:src/deluge/gui/ui/sound_editor.cpp` sets the UI mode whenever `editingKitRow()`, whatever
	///       `affectEntire` says).
	bool affect_entire_held = false;

	/// @brief The kit clip's own `InstrumentClip::affectEntire`, a *resolved output* -- it says what
	///        the clip is set to, not which branch `context_for` took. `kit_global_fx` (the argument)
	///        is what decides that; the two differ exactly on the kit-row shortcut path, where the
	///        clip has affect-entire on but the menu is about to edit one row's params.
	bool affect_entire = false;
	bool is_kit = false;

	/// @brief `soundEditor.selectedNoteRow`: a row was auditioning when Play Direction began its session on a non-kit
	///        clip (`Direction::checkPermissionToBeginSession` in
	///        `origin/main:src/deluge/gui/menu_item/sequence/direction.h`), so it reads and writes the auditioned row,
	///        not the clip. Latched by `MenuScene` when the item is entered or its column focused; cleared by a
	///        re-root.
	bool selected_note_row = false;
	bool is_drum = false;
	/// @brief `SoundEditor::editingNonAudioDrumRow` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the kit's
	///        selected drum is a MIDI or gate drum, whatever the menu edits -- set on the kit's own effects too, where
	///        `is_drum` is not.
	bool non_audio_drum_row = false;
	/// @brief `SoundEditor::editingGateDrumRow` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the kit's selected
	/// drum is a gate drum, whatever the menu edits.
	bool gate_drum_row = false;
	bool is_audio_clip = false;
	bool is_song_view = false;

	/// @brief Is the root UI the **session view specifically** -- legacy's `getRootUI() == &sessionView`
	///        (`MidiLearn::isRelevant` in `origin/main:src/deluge/gui/menu_item/song/midi_learn.h`,
	///        `ConfigureMacros::isRelevant` (`origin/main:src/deluge/gui/menu_item/song/configure_macros.h`))?
	///
	/// `is_song_view` above is the *other* legacy expression, `!rootUIIsClipMinderScreen()` (`SoundEditor::setup` in
	/// `origin/main:src/deluge/gui/ui/sound_editor.cpp`), and one song root serves three views: `setup()` picks
	/// `soundEditorRootMenuSongView` for `sessionView`, `arrangerView` **and** `automationView` alike (the song-view
	/// arm of `SoundEditor::setup`). Only two items in that root care which of the three it is, and they are the two
	/// `getRootUI()` reads above; `relevance::is_session_view` is what spends this field. So the pair is a two-bit
	/// encoding of a three-way fact -- deliberately, and deliberately no finer: nothing in the port distinguishes the
	/// arranger from the song automation view, because no legacy `isRelevant` does either.
	///
	/// `is_song_view` is implied by this one (`sessionView` is not a clip minder), never the
	/// reverse. `context_for` does not enforce that: it transcribes its arguments, exactly as it
	/// does for `performance_view`, and a caller that passes the pair inconsistently gets the same
	/// nonsense legacy would from an impossible UI stack.
	bool is_session_view = false;
	bool is_performance = false;
	bool is_kit_global_fx = false;

	/// @brief The greyout `SoundEditor::getGreyoutColsAndRows` decides by the root UI's type alone
	///        (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), which this layer may not read: the menu's opener sets
	///        it.
	enum class RootGreyout : uint8_t {
		Menu,           ///< Any other root: the menu's own arms decide.
		Nothing,        ///< The keyboard screen, or the automation view's editor: grey nothing.
		SpareSidebar,   ///< The automation view outside its editor: grey all but the sidebar.
		InstrumentClip, ///< An instrument clip view: grey all but the audition column, whatever menu is open.
	};
	RootGreyout root_greyout = RootGreyout::Menu;

	/// @brief Whether the root UI under the menu is the keyboard screen (`getRootUI() ==
	///        &keyboardScreen`), which this layer may not read: the menu's opener sets it.
	///
	/// Not read from `root_greyout`, which is also `Nothing` under the automation view's editor.
	bool root_is_keyboard = false;

	/// @brief Whether the root UI under the menu is the automation view (`getRootUI() == &automationView`), which
	///        this layer may not read: the menu's opener sets it, and the host again whenever the root changes.
	bool root_is_automation = false;

	/// @brief False where `setup()` would have returned `false` outright: a kit whose selected `SoundDrum` has no note
	///        row (the `noteRow == nullptr` return of `SoundEditor::setup` in
	///        `origin/main:src/deluge/gui/ui/sound_editor.cpp`), and a kit with neither affect-entire nor a selected
	///        drum (the last `else` of the kit arm in `SoundEditor::setup`).
	///
	/// `context_for()` returns a value, not `setup()`'s `bool`, so the refusal needs somewhere to
	/// live. Nothing else in this
	/// struct distinguishes those two cases from a legitimately sparse context -- the settings root
	/// also has a null `params`.
	bool resolved = false;

	Root root = Root::Settings;

	/// @name What a target node binds through
	/// The model and addresses a target resolves against, and the host's ports, which its reads and writes go
	/// through (`ports.writes` takes its writes, `ports.controls_sink` the gold knobs' and an effect setting's exact
	/// word).
	/// @{
	const deluge::project::Song* model = nullptr;
	deluge::scenes::bind::Addresses addresses{};
	/// The host's ports; a spec's fakes.
	deluge::scenes::bind::Ports ports{};
	/// @}
};

/// @brief Resolve what a menu opened over @p clip would edit -- `SoundEditor::setup`'s context half,
///        with no UI reads.
///
/// @param song                    the song that owns @p clip; used instead of the `currentSong`
///                                global wherever `setup()` reached for it.
/// @param view                    what the menu opens over, as the view shows it: the clip (none for a song-level
///                                or settings menu), its track and what plays it, the kit row selected, the clip's
///                                affect-entire.
/// @param source_index            which of a `Sound`'s two sources the menu addresses
///                                (`setup()`'s `sourceIndex`).
/// @param performance_view        `setup()`'s `currentUI == &performanceView && !shift`.
/// @param session_or_arranger_root `setup()`'s `!rootUIIsClipMinderScreen()` -- true when
///                                the root UI is the session, arranger, song-automation or
///                                performance view. Also stands in for the song-root arm's
///                                `currentUI == &sessionView || &arrangerView || &automationView`
///                                with shift not held.
/// @param session_view_root       `getRootUI() == &sessionView` -- the *narrower* fact
///                                `session_or_arranger_root` cannot carry, and the only thing that
///                                separates the three views the song-root arm funnels into one.
///                                See `MenuContext::is_session_view`. Implies
///                                `session_or_arranger_root`; passing it true with that false is a
///                                caller bug this function does not police.
/// @param kit_global_fx `setup()`'s `setupKitGlobalFXMenu` member. True to edit the kit's own global FX, false to edit
///                                the selected row. Callers normally pass the clip's `affectEntire`, which is exactly
///                                what both writers of that member set it from (the `SELECT_ENC` arm of
///                                `InstrumentClipMinder::buttonAction` in
///                                `origin/main:src/deluge/model/clip/instrument_clip_minder.cpp`,
///                                `AutomationView::handleSelectEncoderButtonAction` in
///                                `origin/main:src/deluge/gui/views/automation_view.cpp`); the kit-row grid shortcut
///                                (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) is the caller that passes false
///                                with affect-entire on, and it is why this is an argument rather than a re-read of the
///                                clip.
MenuContext context_for(Song& song, const services::MenuView& view, int8_t source_index, bool performance_view,
                        bool session_or_arranger_root, bool session_view_root, bool kit_global_fx);

/// @brief May a session begin on @p node in @p ctx? One rule in place of the
///        per-item `checkPermissionToBeginSession` overrides.
enum class Permission : uint8_t {
	/// @brief The node does not apply here -- `MenuPermission::NO`.
	No,
	/// @brief Open it -- `MenuPermission::YES`.
	Yes,
	/// @brief Push the range picker first -- `MenuPermission::MUST_SELECT_RANGE`
	/// (`origin/main:src/deluge/gui/menu_item/menu_item.h`).
	MustSelectRange,
};

/// @brief Apply the menu's permission rule to @p node in @p ctx.
///
/// Ports the base `MenuItem::checkPermissionToBeginSession` (`origin/main:src/deluge/gui/menu_item/menu_item.cpp`,
/// relevance only) plus the range rule the range-specific overrides share via
/// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`
/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`).
///
/// The range rule fires on `Node::range_specific` -- the items whose override ends in that call --
/// **not** on "has a `bind.param`". An ordinary patched param on a multisampled sound takes the
/// base rule and opens (`Yes`); only a range-specific item on a source with more than one
/// `MultiRange` and none selected gets `MustSelectRange`.
/// @param node The node a session would begin on.
/// @param ctx  The menu's context.
/// @return `Yes`, `No`, or `MustSelectRange`.
Permission permission_for(const Node& node, const MenuContext& ctx);

/// @brief @p ctx as @p node's range rule reads it: about the item's own source (`CustomItem::range_source`) when that
///        is not the menu's, with no range of that source remembered (`main`'s `currentSourceIndex == whichThing`
///        fails); @p ctx itself otherwise.
MenuContext range_rule_context(const Node& node, const MenuContext& ctx);

/// @brief How many ranges the addressed source records.
///
/// The range picker draws one row per range and has to know how many there are; a context with no
/// sound has none.
int32_t range_count(const MenuContext& ctx);

/// @brief The `topNote` of @p index's range, or `INT16_MAX` (legacy's own 32767 sentinel for "the
///        top one") when @p index is out of range.
///
/// The picker's row labels are built from the *previous* range's `topNote + 1` and this range's
/// `topNote` (`MultiRange::getText`, `origin/main:src/deluge/gui/menu_item/multi_range.cpp`). `MultiRange` is a
/// `storage/` type, so the read lives here beside the other model reads rather than in the node
/// table.
int32_t range_top_note(const MenuContext& ctx, int32_t index);

/// @brief The addressed source's recorded ranges, empty where the context edits no sound: the list a range edit
///        rewrites and hands back to the sound writes, as legacy's edge edit builds it (`deluge::bridge::rangesOf`).
std::vector<synth::SourceRange> ranges_of(const MenuContext& ctx);

/// @brief Make sure @p ctx's source has at least one `MultiRange`, and point @p ctx at it when the choice is
///        unambiguous -- `Source::getOrCreateFirstRange()` in `main`'s
///        `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`
///        (`origin/main:src/deluge/gui/ui/sound_editor.cpp`).
///
/// **This is the allocation `permission_for` deliberately does not do** (see this file's `.cpp`,
/// "Not owned by this file"): main's rule *creates* the first range before it answers anything, and can fail with
/// `INSUFFICIENT_RAM`. `permission_for` is a pure predicate, so the menu makes the first range of a range-specific
/// item wherever main runs that rule on one, through @ref ensure_item_first_range.
///
/// On success it also mirrors main's `*previouslySelectedRange = firstRange`: when the choice cannot be ambiguous (a
/// kit row, or exactly one range) `ctx.range_index` is pointed at range 0, exactly as `context_for` does for a context
/// resolved with ranges already present.
///
/// @param ctx The context to top up; only its sound and source matter. A context with no sound is a
///            no-op success -- nothing is being edited that could want a range.
/// @return False only where the sound writes refused the first range -- main's `INSUFFICIENT_RAM`, answered with
///         `MenuPermission::NO`.
bool ensure_first_range(MenuContext& ctx);

/// @brief The first range of the source @p node's range rule is about (`range_rule_context`), made as main's
///        `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`
///        (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) makes it: in the menu's context when the rule is about the
///        menu's own source, else in that other source's.
///
/// The menu runs it where main runs that rule on a range-specific item: a descent or a press on a horizontal item
/// (`MenuWalk::enter_range_picker_if_needed`), a deep link (`MenuScene::on_enter`) and a page switch, which discards
/// the answer (`HorizontalMenu::switchVisiblePage`).
/// @param node The item; one that is not range-specific makes nothing and answers `Yes`.
/// @param ctx  The menu's context, whose range index the first range resolves when the rule is about its source.
/// @return `No` where the sound port refused the first range; `MustSelectRange` where the rule's context edits a sound
///         whose range is still unresolved (more than one, none remembered); `Yes` otherwise.
Permission ensure_item_first_range(const Node& node, MenuContext& ctx);

/// @brief Point @p ctx at range @p index -- `SoundEditor::setCurrentMultiRange`
/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`).
///
/// Out-of-range indices are ignored rather than clamped: legacy indexes `ranges.getElement(i)`
/// unchecked, and every caller here has already clamped against `range_count`.
/// @param ctx   The menu's context, whose source's range is selected.
/// @param index The range to select.
void select_range(MenuContext& ctx, int32_t index);

/// @brief Point @p ctx at oscillator source @p index -- `SoundEditor::setCurrentSource`
///        (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), plus the range reset `SoundEditor::setup`
///        performs alongside it.
///
/// Legacy's per-source bespoke items each hold their own `source_id_` and open with
/// `soundEditor.setCurrentSource(sourceId_)` -- `FileSelector::beginSession`
/// (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`), `sample::LoopPoint::beginSession`
/// (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`), `osc::AudioRecorder::beginSession`
/// (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`). Nothing in the port re-resolves the context on the
/// way into `source1`'s page (see `relevance.h`'s note on why the per-source predicates are named
/// rather than context-driven), so this is where an item that is *about* to hand a context to
/// another scene makes it name its own source.
///
/// Three statements of legacy's, plus one of `setup()`'s:
///
///   * `currentSource` / `currentSourceIndex` / `currentSampleControls`, set by `SoundEditor::setCurrentSource`
///     (`origin/main:src/deluge/gui/ui/sound_editor.cpp`);
///   * adopt the source's first range when none is selected yet, as `setCurrentSource` does;
///   * the range reset in `setup()`, which drops a remembered range whenever the source index moves.
///     Without it, an item on source 1 could open a scene over source **0**'s `MultiRange` -- the
///     one `context_for` resolved -- and edit the wrong sample's markers. Only the source-index
///     half of that condition is ported; the `.cpp` says why the UI half has nothing to clear here.
///
/// @param ctx   The context to re-point. A context with no `sound` (an audio clip, the settings
///              root) is left exactly as it is: there is no source to select, which is precisely
///              why `audio_clip::SampleMarkerEditor::beginSession`
///              (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`) has no such call.
/// @param index Which of `Sound::sourceResources`. Negative, or past the last source, is a no-op --
///              `nodes::MarkerItem` spells "the audio-clip opener, which selects no source" as -1.
void select_source(MenuContext& ctx, int8_t index);

} // namespace deluge::scenes::menu
