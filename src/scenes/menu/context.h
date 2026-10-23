#pragma once

/// @brief `MenuContext` -- what a menu edits -- and the three pure functions that produce it, reach
///        the model through it, and gate entry into a node with it (spec E §4 and §4.1).
///
/// This is the context-resolution half of `SoundEditor::setup`
/// (`src/deluge/gui/ui/sound_editor.cpp:1673-1906`), `SoundEditor::getCurrentModelStack` (`:1999`)
/// and the common rule behind the `checkPermissionToBeginSession` overrides
/// (`SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`, `:1933`), ported as free
/// functions over plain arguments. (`gui/menu_item/` has 18 *files* naming
/// `checkPermissionToBeginSession`; two of them, `menu_item.cpp`/`.h`, are the base rather than
/// overrides.)
///
/// @note **No UI reads** (task-3 ruling 2). `setup()` consults `getCurrentUI()`, `currentUIMode`,
///       `rootUIIsClipMinderScreen()`, `Buttons::isShiftButtonPressed()` and its own
///       `setupKitGlobalFXMenu` member. None of that is reachable from `src/scenes` -- the caller
///       reduces it to `context_for`'s `performance_view` / `session_or_arranger_root` /
///       `session_view_root` / `kit_global_fx` arguments. See `context.cpp` for the branch-by-branch
///       mapping and for what stayed behind.
///
/// @note **`model/model_stack.h` is included by `context.cpp` only**, for the gold-knob boundary below, which builds
///       its stacks from a `ModTarget` until the knobs bind by address.

#include "definitions_cxx.hpp"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/target.h"
#include "scenes/bind/write_sink.h"
#include "scenes/services/mod_target.h" // the gold-knob boundary's POD arguments and results

#include <cstdint>

class Clip;
class MIDICable;
class ModControllableAudio;
class Song;

namespace deluge::project {
class Song;
} // namespace deluge::project

namespace deluge::scenes::menu {

struct Node;

/// @brief Everything a menu item needs to know about what it is editing.
///
/// One value replaces the twelve `soundEditor.current*` members the survey lists
/// (`.superpowers/sdd/2026-08-27-ui-scenes-menus/menu-survey-summary.md`, "SoundEditor"): a menu
/// hook is handed a `const MenuContext&` instead of reading a global.
///
/// It holds what a node edits by address -- the record (`model`), the owners (`addresses`) -- and what it reads and
/// writes through: the host's runtime facts and its sink. Its flags say which kind of context `context_for()`
/// resolved. The engine objects it was built from stay in the builder.
struct MenuContext {
	/// @brief Which root menu `setup()` would have opened (`sound_editor.cpp:1770-1826`).
	///
	/// `Count` is the enumerator count, not a root.
	///
	/// @note `setup()` picks `NoteEditor`/`NoteRowEditor` from a `currentUIMode` this layer may not
	///       read (`noteEditorRootMenu` `:1775`, `noteRowEditorRootMenu` `:1778`) -- so, unlike every
	///       other enumerator here, `context_for` never assigns either one; a caller that wants one
	///       (`gui/ui/shell_menu_entry.cpp`'s `openShellMenu`, `NoteMode`) sets `ctx.root` itself
	///       after resolving the rest of the context (spec E-note-family task 6).
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
	///        edits it reaches through `model`, `addresses`, `facts` and `sink`.
	Song* song = nullptr;
	/// @brief What the gold knobs and the parameter popup act on, until they bind by address.
	ModControllableAudio* mod = nullptr;
	/// @brief The range the menu edits, by index into the source's ranges; -1 for none chosen.
	int16_t range_index = -1;
	/// @brief The sound source the menu edits.
	int8_t source_index = 0;

	/// @brief `soundEditor.currentMIDICable`. `setup()` never assigns it (the MIDI-device menus do,
	///        from their own selection), so `context_for()` always leaves it null.
	MIDICable* midi_cable = nullptr;

	/// @brief Is the AFFECT-ENTIRE button held right now? Legacy's
	///        `currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR`
	///        (`gui/menu_item/unpatched_param.cpp:52` and the 20-odd items that spell the same test).
	///
	/// A UI fact, so -- like `midi_cable` -- `context_for()` never assigns it: the caller sets it,
	/// exactly as it reduces the other UI reads to `context_for`'s `bool` arguments (task-3 ruling 2).
	/// `bindings::write()` is its only reader; with it set on a kit row, a param write goes to every
	/// `SoundDrum` in the kit instead of just the selected one.
	///
	/// @note NOT the same fact as `affect_entire` below. That one is the clip's own
	///       `InstrumentClip::affectEntire` setting, which is true on the kit-row grid-shortcut path
	///       (`sound_editor.cpp:1205-1213`) where legacy still edits one row only; the button being
	///       held is independent of it (`sound_editor.cpp:540-547` sets the UI mode whenever
	///       `editingKitRow()`, whatever `affectEntire` says).
	bool affect_entire_held = false;

	/// @brief The kit clip's own `InstrumentClip::affectEntire`, a *resolved output* -- it says what
	///        the clip is set to, not which branch `context_for` took. `kit_global_fx` (the argument)
	///        is what decides that; the two differ exactly on the kit-row shortcut path, where the
	///        clip has affect-entire on but the menu is about to edit one row's params.
	bool affect_entire = false;
	bool is_kit = false;
	bool is_drum = false;
	bool is_audio_clip = false;
	bool is_song_view = false;

	/// @brief Is the root UI the **session view specifically** -- legacy's `getRootUI() == &sessionView`
	///        (`gui/menu_item/song/midi_learn.h:35-37`,
	///        `gui/menu_item/song/configure_macros.h:35-37`)?
	///
	/// `is_song_view` above is the *other* legacy expression, `!rootUIIsClipMinderScreen()`
	/// (`sound_editor.cpp:1687`), and one song root serves three views: `setup()` picks
	/// `soundEditorRootMenuSongView` for `sessionView`, `arrangerView` **and** `automationView`
	/// alike (`sound_editor.cpp:1819-1822`). Only two items in that root care which of the three it
	/// is, and they are the two `getRootUI()` reads above; `relevance::is_session_view` is what
	/// spends this field. So the pair is a two-bit encoding of a three-way fact -- deliberately, and
	/// deliberately no finer: nothing in the port distinguishes the arranger from the song
	/// automation view, because no legacy `isRelevant` does either.
	///
	/// `is_song_view` is implied by this one (`sessionView` is not a clip minder), never the
	/// reverse. `context_for` does not enforce that: it transcribes its arguments, exactly as it
	/// does for `performance_view`, and a caller that passes the pair inconsistently gets the same
	/// nonsense legacy would from an impossible UI stack.
	bool is_session_view = false;
	bool is_performance = false;
	bool is_kit_global_fx = false;

	/// @brief False where `setup()` would have returned `false` outright: a kit whose selected
	///        `SoundDrum` has no note row (`sound_editor.cpp:1719-1721`), and a kit with neither
	///        affect-entire nor a selected drum (`:1677-1684`).
	///
	/// An addition to the brief's field list, forced by the signature: `context_for()` returns a
	/// value, not `setup()`'s `bool`, so the refusal needs somewhere to live. Nothing else in this
	/// struct distinguishes those two cases from a legitimately sparse context -- the settings root
	/// also has a null `params`.
	bool resolved = false;

	Root root = Root::Settings;

	/// @name What a target node binds through
	/// The model and addresses a target resolves against, the host's runtime facts, and the sink its writes go to.
	/// @{
	const deluge::project::Song* model = nullptr;
	deluge::scenes::bind::Addresses addresses{};
	const deluge::scenes::bind::RuntimeFacts* facts = nullptr;
	deluge::scenes::bind::WriteSink* sink = nullptr;
	/// @}
};

/// @brief Resolve what a menu opened over @p clip would edit -- `SoundEditor::setup`'s context half
///        (`sound_editor.cpp:1686-1826` and `:1828-1859`), with no UI reads.
///
/// @param song                    the song that owns @p clip; used instead of the `currentSong`
///                                global wherever `setup()` reached for it (task-3 ruling 3).
/// @param clip                    the clip being edited, or null for a song-level / settings menu.
/// @param source_index            which of a `Sound`'s two sources the menu addresses
///                                (`setup()`'s `sourceIndex`).
/// @param performance_view        `setup()`'s `currentUI == &performanceView && !shift` (`:1816`).
/// @param session_or_arranger_root `setup()`'s `!rootUIIsClipMinderScreen()` (`:1687`) -- true when
///                                the root UI is the session, arranger, song-automation or
///                                performance view. Also stands in for `:1819`'s
///                                `currentUI == &sessionView || &arrangerView || &automationView`
///                                with shift not held.
/// @param session_view_root       `getRootUI() == &sessionView` -- the *narrower* fact
///                                `session_or_arranger_root` cannot carry, and the only thing that
///                                separates the three views `:1819-1822` funnels into one song root.
///                                See `MenuContext::is_session_view`. Implies
///                                `session_or_arranger_root`; passing it true with that false is a
///                                caller bug this function does not police.
/// @param kit_global_fx           `setup()`'s `setupKitGlobalFXMenu` member (`:1709`). True to edit
///                                the kit's own global FX, false to edit the selected row. Callers
///                                normally pass the clip's `affectEntire`, which is exactly what
///                                both writers of that member set it from
///                                (`model/clip/instrument_clip_minder.cpp:393-417`,
///                                `gui/views/automation_view.cpp:1623-1625`); the kit-row grid
///                                shortcut (`sound_editor.cpp:1205-1213`) is the caller that passes
///                                false with affect-entire on, and it is why this is an argument
///                                rather than a re-read of the clip.
MenuContext context_for(Song& song, Clip* clip, int8_t source_index, bool performance_view,
                        bool session_or_arranger_root, bool session_view_root, bool kit_global_fx);

/// @name The gold-knob model boundary (`services::ModKnobs`, E4 task 2)
///
/// Three transactions that are not about menus at all, living here because each one needs a
/// `ModelStack` it can call members on, and this file
/// plus `bindings.cpp` are the only two the `scenes-model-stack-outside-context` ratchet
/// (`scripts/check_layer_rules.py`) lets include `model/model_stack.h`. The alternative was a third
/// file on that rule's allow-list. Everything they take and return is POD over forward-declared
/// pointers (`scenes/services/mod_target.h`), so `mod_knobs.cpp` -- which owns all of the *policy*
/// these three deliberately leave out (the popup, the MIDI feedback, the indicator blink, the
/// repaint, every arm's UI read) -- never names a stack type.
/// @{

/// @brief Point @p target at what @p counter's active mod controllable is -- the stack half of
///        `View::setActiveModControllableTimelineCounter` (`gui/views/view.cpp:1884-1916`).
///
/// `:1868-1870`'s `getTimelineCounterToRecordTo()` redirection, `:1872-1873`'s stack setup, and the
/// `getActiveModControllable` / `addOtherTwoThingsButNoNoteRow` fork at `:1875-1881`. Everything
/// else in that function (the LED and indicator refresh, the VU-meter sidebar invalidation, the
/// MIDI feedback) is `ModKnobs`'.
///
/// @param target  Overwritten with the resolved song / timeline counter / note row / mod
///                controllable / param manager. Its `mod_pos`/`mod_length`/`mod_note_row_id` are
///                left alone -- legacy's are separate globals and this call does not touch them.
/// @param song    The song, in place of `currentSong` (`:1873`; `scenes-currentSong`).
/// @param counter The timeline counter to point at, or null for "no timeline".
void mod_target_from_timeline(services::ModTarget& target, Song& song, TimelineCounter* counter);

/// @brief Turn one gold knob against the model -- `View::modEncoderAction` (`view.cpp:835-859`),
///        `modEncoderAction_nonExistentParam` (`:843-852`) and `modEncoderAction_existentParam`
///        (`:853-948`), minus the popup and minus every UI read.
///
/// One call, not a read/decide/write triple, because legacy resolves the param stack exactly once:
/// `getModelStackWithParam` (`:971-1010`) has side effects --
/// `TimelineCounter::possiblyCloneForArrangementRecording`, and the re-fetch that follows it -- that
/// must not happen twice for one turn. @p next_knob_pos is how the caller keeps its own policy
/// (`calculateKnobPosForModEncoderTurn`) out here anyway.
///
/// The popup deliberately does *not* happen inside: legacy shows it at `:899` between reading the
/// old position and writing the new one, but it reads nothing the write changes, so the caller
/// shows it afterwards from `ModTurn`'s `kind`/`param_id`/`new_knob_pos` -- which are filled in on
/// the unchanged-value path too, exactly as legacy's `:902` early return happens *after* the popup.
///
/// @param target        What is being edited; updated in place if the arrangement-recording clone
///                      replaced it (`:992-1000`).
/// @param which_encoder 0 for the lower gold knob (`mod0`), 1 for the upper (`mod1`) --
///                      `hid/encoders.h:80-81`.
/// @param offset        The (already accelerated) signed turn.
/// @param next_knob_pos The caller's `calculateKnobPosForModEncoderTurn`.
/// @return What happened; `has_controllable` false means nothing did.
services::ModTurn mod_encoder_turn(services::ModTarget& target, int32_t which_encoder, int32_t offset,
                                   services::NextKnobPosFn next_knob_pos);

/// @brief What param a gold knob addresses right now -- `View::getModelStackWithParam`
///        (`view.cpp:988-1027`) as `View::getParameterNameFromModEncoder` (`:1029-1033`) calls it.
///
/// @warning Runs `getModelStackWithParam`'s side effects, clone included, exactly as legacy's own
///          naming path does. It is not a free observation.
/// @param target        What is being edited; updated in place, as in `mod_encoder_turn`.
/// @param which_encoder 0 for the lower gold knob, 1 for the upper.
/// @return The param's kind and id, or `param_exists == false`.
services::ModParamInfo mod_param_info(services::ModTarget& target, int32_t which_encoder);

/// @brief Permanently grab automation values from @p target's `mod_pos` -- `View::setModRegion`'s
///        held-note branch (`view.cpp:1945-1955`).
///
/// The caller has already checked everything `:1928-1932` tests except the last clause; this runs
/// `ParamManagerForTimeline::grabValuesFromPos` (`:1937-1938`), which needs the stack.
/// @param target What is being edited. A no-op unless it has both a mod controllable and a param
///               manager with main param collections.
void mod_region_grab_values(const services::ModTarget& target);

/// @brief Read what a gold knob's indicator column should show -- the model half of
///        `View::setKnobIndicatorLevel` (`gui/views/view.cpp:1420-1485`).
///
/// Resolves the param stack with `getParamFromModEncoder(whichModEncoder, …, false)` -- legacy's
/// own third argument at `:1407-1408`, which is why this is **not** `mod_param_info`: that one goes
/// through `getModelStackWithParam` and runs the arrangement-recording clone, and levelling an
/// indicator must not. Every clamp, stutter bucket, offset and patch-cable fold legacy then applies
/// stays in `mod_knobs.cpp`.
/// @param target        What is being edited. Not modified: no clone runs.
/// @param which_encoder 0 for the lower gold knob, 1 for the upper.
/// @return The reads, or a default-constructed value when there is no mod controllable.
services::ModIndicatorInfo mod_indicator_info(const services::ModTarget& target, int32_t which_encoder);

/// @brief Press or release a gold knob -- the model half of
///        `View::modEncoderButtonAction_changeModControllable` (`view.cpp:1380-1394`).
///
/// `:1364-1366`'s stack copy and `:1368-1369`'s
/// `ModControllable::modEncoderButtonAction(whichModEncoder, on, modelStack)`. The
/// `instrumentBeenEdited()` that follows it, the indicator re-level and the sound-editor
/// `readValueAgain()` are all `ModKnobs`'.
/// @param target        What is being edited.
/// @param which_encoder 0 for the lower gold knob, 1 for the upper.
/// @param on            Press (true) or release (false).
/// @return Legacy's `anyEditingDone` (`:1370`).
bool mod_encoder_button(const services::ModTarget& target, int32_t which_encoder, bool on);

/// @brief Set the gold-knob mode of @p target's knob owner (the song's own, an output's, a kit row's kit's) to @p mode
///        through the sink; an owner outside the song takes it in its engine field alone.
/// @return Whether the sink took it.
bool mod_knob_mode(const services::ModTarget& target, uint8_t mode);

/// @brief Delete a gold knob's param automation -- `View::modEncoderButtonAction_deleteAutomation`
///        (`view.cpp:1368-1378`).
///
/// Includes the `ActionType::AUTOMATION_DELETE` action-log entry (`:1357`) and the
/// `AutoParam::deleteAutomation` call (`:1358`); the "Automation deleted" popup is `ModKnobs`',
/// raised as an `Action::Kind::Popup` rather than by calling `display` from the model layer.
/// @param target        What is being edited.
/// @param which_encoder 0 for the lower gold knob, 1 for the upper.
/// @return True when there was an `autoParam` and its automation was deleted -- legacy's `:1356`
///         test, and therefore whether the popup is shown.
bool mod_delete_automation(const services::ModTarget& target, int32_t which_encoder);
/// @}

/// @brief May a session begin on @p node in @p ctx? Spec E §4.1's one rule in place of the
///        per-item `checkPermissionToBeginSession` overrides.
enum class Permission : uint8_t {
	/// @brief The node does not apply here -- `MenuPermission::NO`.
	No,
	/// @brief Open it -- `MenuPermission::YES`.
	Yes,
	/// @brief Push the range picker first -- `MenuPermission::MUST_SELECT_RANGE`.
	MustSelectRange,
};

/// @brief Apply spec E §4.1's permission rule to @p node in @p ctx.
///
/// Ports the base `MenuItem::checkPermissionToBeginSession` (`gui/menu_item/menu_item.cpp:27-31`,
/// relevance only) plus the range rule the range-specific overrides share via
/// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam` (`sound_editor.cpp:1988-2015`).
///
/// The range rule fires on `Node::range_dependent` -- legacy's `MenuItem::isRangeDependent()`
/// (`gui/menu_item/menu_item.h:202`) -- **not** on "has a `bind.param`". An ordinary patched param
/// on a multisampled sound takes the base rule and opens (`Yes`); only a range-dependent item on a
/// source with more than one `MultiRange` and none selected gets `MustSelectRange`.
Permission permission_for(const Node& node, const MenuContext& ctx);

/// @brief How many ranges the addressed source records.
///
/// The range picker draws one row per range and has to know how many there are; a context with no
/// sound has none.
int32_t range_count(const MenuContext& ctx);

/// @brief The `topNote` of @p index's range, or `INT16_MAX` (legacy's own 32767 sentinel for "the
///        top one") when @p index is out of range.
///
/// The picker's row labels are built from the *previous* range's `topNote + 1` and this range's
/// `topNote` (`MultiRange::getText`, `gui/menu_item/multi_range.cpp:313-358`). `MultiRange` is a
/// `storage/` type, so the read lives here beside the other model reads rather than in the node
/// table.
int32_t range_top_note(const MenuContext& ctx, int32_t index);

/// @brief Make sure @p ctx's source has at least one `MultiRange`, and point @p ctx at it when the
///        choice is unambiguous -- `Source::getOrCreateFirstRange()` at `sound_editor.cpp:1993`.
///
/// **This is the allocation `permission_for` deliberately does not do** (see this file's `.cpp`,
/// "Owed to E2"): legacy's `checkPermissionToBeginSessionForRangeSpecificParam` *creates* the first
/// range before it answers anything, and can fail with `INSUFFICIENT_RAM`. `permission_for` stayed a
/// pure predicate, so **every** caller that actually opens a range-dependent item does this first.
/// There are two, because legacy has two ways in and makes no distinction between them:
/// `MenuScene::enter_child` for a descent, and `MenuScene::on_enter` for a `SceneId::arg` deep link
/// (the eight gated `openShellMenu` sites and the shortcut-pad jump's `Action::Kind::Replace`).
/// F3 task 4 added the second after the ported file selector, reached by the deep link, refused to
/// open its browser on a source with no ranges.
///
/// On success it also mirrors legacy's `*previouslySelectedRange = firstRange` (`:1951`): when the
/// choice cannot be ambiguous (a kit row, or exactly one range) `ctx.range_index` is pointed at
/// range 0, exactly as `context_for` does for a context resolved with ranges already present.
///
/// @param ctx The context to top up; only its sound and source matter. A context with no sound is a
///            no-op success -- nothing is being edited that could want a range.
/// @return False only where the sink refused the first range -- legacy's `INSUFFICIENT_RAM`, its
///         `MenuPermission::NO` arm at `:1939-1942`.
bool ensure_first_range(MenuContext& ctx);

/// @brief Point @p ctx at range @p index -- `SoundEditor::setCurrentMultiRange`
///        (`sound_editor.cpp:1983-1986`), plus `Source::defaultRangeI`, which the picker keeps in
///        step with its own cursor (`multi_range.cpp:239`).
///
/// Out-of-range indices are ignored rather than clamped: legacy indexes `ranges.getElement(i)`
/// unchecked, and every caller here has already clamped against `range_count`.
void select_range(MenuContext& ctx, int32_t index);

/// @brief Point @p ctx at oscillator source @p index -- `SoundEditor::setCurrentSource`
///        (`sound_editor.cpp:242-251 setCurrentSource`), plus the range reset `SoundEditor::setup`
///        performs alongside it (`:1786-1788`).
///
/// Legacy's per-source bespoke items each hold their own `source_id_` and open with
/// `soundEditor.setCurrentSource(sourceId_)` -- `FileSelector::beginSession`
/// (`gui/menu_item/file_selector.cpp:38`), `sample::LoopPoint::beginSession`
/// (`gui/menu_item/sample/loop_point.cpp:68`), `osc::AudioRecorder::beginSession`
/// (`gui/menu_item/osc/audio_recorder.h:38`). Nothing in the port re-resolves the context on the
/// way into `source1`'s page (see `relevance.h`'s note on why the per-source predicates are named
/// rather than context-driven), so this is where an item that is *about* to hand a context to
/// another scene makes it name its own source.
///
/// Three statements of legacy's, plus one of `setup()`'s:
///
///   * `currentSource` / `currentSourceIndex` / `currentSampleControls` (`:205-207`);
///   * `:209-211` -- adopt the source's first range when none is selected yet;
///   * `setup()`'s `:1786-1788`, which drops a remembered range whenever the source index moves.
///     Without it, an item on source 1 could open a scene over source **0**'s `MultiRange` -- the
///     one `context_for` resolved -- and edit the wrong sample's markers. Only the source-index
///     half of that condition is ported; the `.cpp` says why the UI half has nothing to clear here.
///
/// @param ctx   The context to re-point. A context with no `sound` (an audio clip, the settings
///              root) is left exactly as it is: there is no source to select, which is precisely
///              why `audio_clip::SampleMarkerEditor::beginSession`
///              (`gui/menu_item/audio_clip/sample_marker_editor.cpp:43-51`) has no such call.
/// @param index Which of `Sound::sources`. Negative, or past the last source, is a no-op --
///              `nodes::MarkerItem` spells "the audio-clip opener, which selects no source" as -1.
void select_source(MenuContext& ctx, int8_t index);

} // namespace deluge::scenes::menu
