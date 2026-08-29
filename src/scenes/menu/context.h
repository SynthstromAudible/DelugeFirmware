#pragma once

/// @brief `MenuContext` -- what a menu edits -- and the three pure functions that produce it, reach
///        the model through it, and gate entry into a node with it (spec E §4 and §4.1).
///
/// This is the context-resolution half of `SoundEditor::setup`
/// (`src/deluge/gui/ui/sound_editor.cpp:1618-1851`), `SoundEditor::getCurrentModelStack` (`:1982`)
/// and the common rule behind the 18 `checkPermissionToBeginSession` overrides
/// (`SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`, `:2035`), ported as free
/// functions over plain arguments.
///
/// @note **No UI reads** (task-3 ruling 2). `setup()` consults `getCurrentUI()`, `currentUIMode`,
///       `rootUIIsClipMinderScreen()`, `Buttons::isShiftButtonPressed()` and its own
///       `setupKitGlobalFXMenu` member. None of that is reachable from `src/scenes` -- the caller
///       reduces it to `context_for`'s `performance_view` / `session_or_arranger_root` arguments.
///       See `context.cpp` for the branch-by-branch mapping and for what stayed behind.
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
MenuContext context_for(Song& song, Clip* clip, int8_t source_index, bool performance_view,
                        bool session_or_arranger_root);

/// @brief Build the model stack a binding reads and writes through -- the port of
///        `SoundEditor::getCurrentModelStack` (`sound_editor.cpp:1982-2005`).
///
/// @param memory `MODEL_STACK_MAX_SIZE` bytes supplied by the caller (a `ModelStack` is built in
///               place, never allocated -- this library takes no heap).
/// @return a stack whose `modControllable` is `ctx.mod` and whose `paramManager` is `ctx.params`.
ModelStackWithThreeMainThings* model_stack_for(const MenuContext& ctx, void* memory);

/// @brief May a session begin on @p node in @p ctx? Spec E §4.1's one rule in place of the 18
///        `checkPermissionToBeginSession` overrides.
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
/// relevance only) plus the range rule the 18 overrides share via
/// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam` (`sound_editor.cpp:2035-2060`).
Permission permission_for(const Node& node, const MenuContext& ctx);

} // namespace deluge::scenes::menu
