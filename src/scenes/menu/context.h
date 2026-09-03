#pragma once

/// @brief `MenuContext` -- what a menu edits -- and the three pure functions that produce it, reach
///        the model through it, and gate entry into a node with it (spec E §4 and §4.1).
///
/// This is the context-resolution half of `SoundEditor::setup`
/// (`src/deluge/gui/ui/sound_editor.cpp:1618-1851`), `SoundEditor::getCurrentModelStack` (`:1982`)
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
///       `kit_global_fx` arguments. See `context.cpp` for the branch-by-branch mapping and for
///       what stayed behind.
///
/// @note **`model/model_stack.h` is included by `context.cpp` only** (task-3 ruling 6) -- and, from
///       Task 4, by `bindings.cpp`. `model_stack_for()` below is the single entry point the rest of
///       `src/scenes` uses to reach a `ModelStack`, which is why this header forward declares
///       `ModelStackWithThreeMainThings` rather than including the model-stack header.

#include "definitions_cxx.hpp" // VoicePriority (an enum -- a pointer to it needs the definition)

#include <cstdint>

class ArpeggiatorSettings;
class Clip;
class MIDICable;
class ModControllableAudio;
class ModelStackWithThreeMainThings;
class MultiRange;
class Output;
class ParamManagerForTimeline;
class SampleControls;
class SideChain;
class Song;
class Sound;
class Source;

namespace deluge::scenes::menu {

struct Node;

/// @brief Everything a menu item needs to know about what it is editing.
///
/// One value replaces the twelve `soundEditor.current*` members the survey lists
/// (`.superpowers/sdd/2026-08-27-ui-scenes-menus/menu-survey-summary.md`, "SoundEditor"): a menu
/// hook is handed a `const MenuContext&` instead of reading a global.
///
/// Every pointer may be null -- which combination is null is exactly what `context_for()`'s branch
/// resolved, and is meaningful (e.g. `sound == nullptr` on a MIDI clip, `params == nullptr` in the
/// settings root).
struct MenuContext {
	/// @brief Which root menu `setup()` would have opened (`sound_editor.cpp:1715-1771`).
	///
	/// `Count` is the enumerator count, not a root.
	///
	/// @note `setup()`'s two `currentUIMode`-driven roots -- `noteEditorRootMenu` (`:1719`) and
	///       `noteRowEditorRootMenu` (`:1722`) -- have no enumerator here: they are selected from a
	///       UI mode this layer may not read, so they stay with the caller (spec E's `NoteClipScene`).
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
		Count,
	};

	Song* song = nullptr;
	Clip* clip = nullptr;
	Output* output = nullptr;
	Sound* sound = nullptr;
	ModControllableAudio* mod = nullptr;
	ParamManagerForTimeline* params = nullptr;
	ArpeggiatorSettings* arp = nullptr;
	MultiRange* range = nullptr;
	int16_t range_index = -1;
	int8_t source_index = 0;
	Source* source = nullptr;
	SampleControls* sample_controls = nullptr;
	VoicePriority* priority = nullptr;
	SideChain* sidechain = nullptr;

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
	///       (`sound_editor.cpp:1150-1158`) where legacy still edits one row only; the button being
	///       held is independent of it (`sound_editor.cpp:502-509` sets the UI mode whenever
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
	bool is_performance = false;
	bool is_kit_global_fx = false;

	/// @brief False where `setup()` would have returned `false` outright: a kit whose selected
	///        `SoundDrum` has no note row (`sound_editor.cpp:1664-1666`), and a kit with neither
	///        affect-entire nor a selected drum (`:1677-1684`).
	///
	/// An addition to the brief's field list, forced by the signature: `context_for()` returns a
	/// value, not `setup()`'s `bool`, so the refusal needs somewhere to live. Nothing else in this
	/// struct distinguishes those two cases from a legitimately sparse context -- the settings root
	/// also has a null `params`.
	bool resolved = false;

	Root root = Root::Settings;
};

/// @brief Resolve what a menu opened over @p clip would edit -- `SoundEditor::setup`'s context half
///        (`sound_editor.cpp:1631-1771` and `:1811-1842`), with no UI reads.
///
/// @param song                    the song that owns @p clip; used instead of the `currentSong`
///                                global wherever `setup()` reached for it (task-3 ruling 3).
/// @param clip                    the clip being edited, or null for a song-level / settings menu.
/// @param source_index            which of a `Sound`'s two sources the menu addresses
///                                (`setup()`'s `sourceIndex`).
/// @param performance_view        `setup()`'s `currentUI == &performanceView && !shift` (`:1761`).
/// @param session_or_arranger_root `setup()`'s `!rootUIIsClipMinderScreen()` (`:1632`) -- true when
///                                the root UI is the session, arranger, song-automation or
///                                performance view. Also stands in for `:1764`'s
///                                `currentUI == &sessionView || &arrangerView || &automationView`
///                                with shift not held.
/// @param kit_global_fx           `setup()`'s `setupKitGlobalFXMenu` member (`:1654`). True to edit
///                                the kit's own global FX, false to edit the selected row. Callers
///                                normally pass the clip's `affectEntire`, which is exactly what
///                                both writers of that member set it from
///                                (`model/clip/instrument_clip_minder.cpp:388-390`,
///                                `gui/views/automation_view.cpp:1601-1603`); the kit-row grid
///                                shortcut (`sound_editor.cpp:1150-1158`) is the caller that passes
///                                false with affect-entire on, and it is why this is an argument
///                                rather than a re-read of the clip.
MenuContext context_for(Song& song, Clip* clip, int8_t source_index, bool performance_view,
                        bool session_or_arranger_root, bool kit_global_fx);

/// @brief Build the model stack a binding reads and writes through -- the port of
///        `SoundEditor::getCurrentModelStack` (`sound_editor.cpp:1982-2005`).
///
/// @pre @p ctx came from `context_for` with `ctx.resolved == true`, and either `ctx.is_song_view` or
///      `ctx.clip != nullptr`. A settings-root context (no clip, no song view) has no timeline
///      counter and would yield a stack over a null one; legacy failed the same case louder, by
///      dereferencing a null `instrument` at `:1989`.
/// @param memory `MODEL_STACK_MAX_SIZE` bytes supplied by the caller (a `ModelStack` is built in
///               place, never allocated -- this library takes no heap).
///
/// @note **Declare that buffer `alignas(ModelStackWithAutoParam) char[MODEL_STACK_MAX_SIZE]`.**
///       Every scratch buffer in `src/scenes` that backs a model stack does (fix round 2, M4);
///       `bindings.cpp` and `context.cpp` used to be split half and half between the aligned form
///       and legacy's bare `char[MODEL_STACK_MAX_SIZE]`, which read as a deliberate distinction and
///       was not one. A `ModelStack*` is placement-constructed into these bytes, and the widest
///       member of the family is `ModelStackWithAutoParam`; a bare `char[]` has alignment 1, so the
///       stack objects land at whatever offset the frame happens to give them. x86-64 tolerates
///       that, the ARM target need not. Legacy uses the bare idiom throughout, so this is not a bug
///       being fixed here so much as one not being inherited -- but it is uniform in this layer, on
///       purpose, and a new scratch buffer should copy the aligned spelling.
/// @return a stack whose `modControllable` is `ctx.mod` and whose `paramManager` is `ctx.params`.
ModelStackWithThreeMainThings* model_stack_for(const MenuContext& ctx, void* memory);

/// @brief Tell the model that a sample marker moved -- the tail of `SampleMarkerEditor::writeValue`
///        (`gui/ui/sample_marker_editor.cpp:203-215`).
///
/// Lives here, beside `model_stack_for`, for the reason the `scenes-model-stack-outside-context`
/// rule exists: both arms need a `ModelStack` they can call a member on
/// (`ModelStackWithTimelineCounter::resumePlayback`'s argument, and `addSoundFlags()`), and
/// `model_stack_for`'s return type is only forward declared everywhere else in `src/scenes`. The
/// alternative was a third file on that rule's allow-list.
///
/// @param ctx                What the marker was written to.
/// @param marker             Which marker moved -- `Sound::sampleZoneChanged`'s first argument.
/// @param resume_audio_clip  Only meaningful on an audio clip: was the clip playing a voice sample
///                           before the write, so playback should be resumed after it? Legacy
///                           computes this *before* `unassignVoiceSample` and passes it down the
///                           same way (`:155-161`, `:203-209`).
void marker_written(const MenuContext& ctx, MarkerType marker, bool resume_audio_clip);

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
/// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam` (`sound_editor.cpp:1933-1960`).
///
/// The range rule fires on `Node::range_dependent` -- legacy's `MenuItem::isRangeDependent()`
/// (`gui/menu_item/menu_item.h:202`) -- **not** on "has a `bind.param`". An ordinary patched param
/// on a multisampled sound takes the base rule and opens (`Yes`); only a range-dependent item on a
/// source with more than one `MultiRange` and none selected gets `MustSelectRange`.
Permission permission_for(const Node& node, const MenuContext& ctx);

/// @brief How many `MultiRange`s the addressed source has -- `std::ssize(source->ranges)`.
///
/// Exposed (E2 task 9) because the range picker draws one row per range and has to know how many
/// there are; a null `ctx.source` has none.
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
///        choice is unambiguous -- `Source::getOrCreateFirstRange()` at `sound_editor.cpp:1938`.
///
/// **This is the allocation `permission_for` deliberately does not do** (see this file's `.cpp`,
/// "Owed to E2"): legacy's `checkPermissionToBeginSessionForRangeSpecificParam` *creates* the first
/// range before it answers anything, and can fail with `INSUFFICIENT_RAM`. `permission_for` stayed a
/// pure predicate, so the caller that actually opens a range-dependent item does this first --
/// `MenuScene::enter_child`, which is the port of the call site.
///
/// On success it also mirrors legacy's `*previouslySelectedRange = firstRange` (`:1951`): when the
/// choice cannot be ambiguous (a kit row, or exactly one range) `ctx.range`/`ctx.range_index` are
/// pointed at range 0, exactly as `context_for` does for a context resolved with ranges already
/// present.
///
/// @param ctx The context to top up; only `ctx.source` matters. A null source is a no-op success --
///            nothing is being edited that could want a range.
/// @return False only on `INSUFFICIENT_RAM`, i.e. legacy's `MenuPermission::NO` arm at `:1939-1942`.
bool ensure_first_range(MenuContext& ctx);

/// @brief Point @p ctx at range @p index -- `SoundEditor::setCurrentMultiRange`
///        (`sound_editor.cpp:1928-1931`), plus `Source::defaultRangeI`, which the picker keeps in
///        step with its own cursor (`multi_range.cpp:239`).
///
/// Out-of-range indices are ignored rather than clamped: legacy indexes `ranges.getElement(i)`
/// unchecked, and every caller here has already clamped against `range_count`.
void select_range(MenuContext& ctx, int32_t index);

/// @brief Point @p ctx at oscillator source @p index -- `SoundEditor::setCurrentSource`
///        (`sound_editor.cpp:204-212 setCurrentSource`), plus the range reset `SoundEditor::setup`
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
