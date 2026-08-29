#pragma once

/// @brief A real `Song` (and the clips/instruments a `MenuContext` is resolved from), built
///        directly on the host -- no `deluge_main()`, no UI, no SD card.
///
/// **Why direct construction and not the loadcheck shape** (task-3 ruling 1): the legacy bring-up
/// that precedes a song, `deluge_boot()` (`src/deluge/deluge.cpp:634`), is almost entirely device
/// bring-up -- OLED probe, control surface, CV DAC, USB host/peripheral, PIC boot info -- and it
/// ends by calling `setupBlankSong()` (`:371`), whose song is built out of `Song`'s own members
/// plus `Song::ensureAtLeastOneSessionClip()` (`model/song/song.cpp:337`). Only two things in that
/// path are actually load-bearing for owning a `Song`:
///
///   * the general-memory allocator, which self-initialises on first use
///     (`GeneralMemoryAllocator::get()` -> `deluge::memory::init_heaps()`,
///     `memory/general_memory_allocator.cpp:131`), so nothing has to call it first; and
///   * `currentSong`, which the model's own stack helpers read.
///
/// and `ensureAtLeastOneSessionClip`'s own fallback branch (`song.cpp:378-403`, the `couldntLoad`
/// label taken whenever the card has no preset to load) is a pure-model recipe for a synth clip:
/// `StorageManager::createNewInstrument` + `setupAsDefaultSynth` + `setAudioInstrument` +
/// `addOutput`. This header replays exactly that branch, so no `startupConditionalTask` hook and
/// no change to `deluge_scenes_spec`'s generated `main()` is needed.
///
/// Determinism: the `scenes_spec` ctest row runs with `DELUGE_HOST_DETERMINISTIC=1` and
/// `DELUGE_SD_ROOT=tests/fixtures/card` (`sim/scenes_spec/CMakeLists.txt`); this fixture touches
/// neither the clock nor the card, so the per-spec rows are equally deterministic.
///
/// Ownership: fixtures are allocated out of the deluge heap and never freed -- a spec process is
/// short-lived and `quick_exit`s, and tearing a `Song` down drags in `AudioEngine::killAllVoices`
/// and the UI globals this header exists to avoid.

#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include <new>

namespace deluge::scenes::spec {

/// @brief A song plus the one clip a spec resolves a `MenuContext` from.
struct SongFixture {
	Song* song = nullptr;
	InstrumentClip* clip = nullptr;
	Instrument* instrument = nullptr;
};

namespace detail {

template <typename T, typename... Args>
inline T* place(Args&&... args) {
	void* memory = deluge::memory::alloc_fast(sizeof(T));
	return memory != nullptr ? new (memory) T(static_cast<Args&&>(args)...) : nullptr;
}

/// @brief `setupBlankSong()`'s song half (`deluge.cpp:372-383`) minus its two UI calls
///        (`setRootUILowLevel`, `setUIForLoadedSong`) and minus `AudioEngine`'s reverb pull.
inline Song* make_bare_song() {
	Song* song = place<Song>();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	song->setupDefault();
	return song;
}

/// @brief Attach `instrument` to `clip` and to `song` -- `ensureAtLeastOneSessionClip`'s tail
///        (`song.cpp:397-401`), without the playback re-sync a spec has no clock for.
inline void attach(Song* song, InstrumentClip* clip, Instrument* instrument, ParamManagerForTimeline& params) {
	clip->setAudioInstrument(instrument, song, true, &params);
	song->addOutput(instrument);
	song->setCurrentClip(clip);
}

inline InstrumentClip* make_session_clip(Song* song) {
	InstrumentClip* clip = place<InstrumentClip>(song);
	clip->loopLength = kDefaultClipLength << song->insideWorldTickMagnitude;
	(void)song->sessionClips.insertClipAt(clip, 0);
	return clip;
}

} // namespace detail

/// @brief A song whose one session clip drives a default `SoundInstrument` --
///        `ensureAtLeastOneSessionClip`'s `couldntLoad` branch (`song.cpp:378-403`) verbatim.
inline SongFixture make_synth_song() {
	SongFixture f;
	f.song = detail::make_bare_song();
	f.clip = detail::make_session_clip(f.song);

	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->dirPath = "SYNTHS";
	synth->name = "0";
	synth->setupAsDefaultSynth(&params);

	detail::attach(f.song, f.clip, synth, params);
	f.instrument = synth;
	return f;
}

/// @brief A song whose one session clip drives a `MIDIInstrument` (no `Sound`, no param manager of
///        its own beyond the clip's) -- the `OutputType::MIDI_OUT` arm of `setup()`'s "else" branch.
inline SongFixture make_midi_song() {
	SongFixture f;
	f.song = detail::make_bare_song();
	f.clip = detail::make_session_clip(f.song);

	ParamManagerForTimeline params;
	params.setupMIDI();
	auto* midi = static_cast<MIDIInstrument*>(StorageManager::createNewInstrument(OutputType::MIDI_OUT, &params));

	detail::attach(f.song, f.clip, midi, params);
	f.instrument = midi;
	return f;
}

/// @brief A song whose one session clip drives a `Kit` with no drums yet.
inline SongFixture make_kit_song() {
	SongFixture f;
	f.song = detail::make_bare_song();
	f.clip = detail::make_session_clip(f.song);

	ParamManagerForTimeline params;
	params.setupUnpatched();
	GlobalEffectableForClip::initParams(&params);
	auto* kit = static_cast<Kit*>(StorageManager::createNewInstrument(OutputType::KIT, &params));
	kit->dirPath = "KITS";
	kit->name = "0";

	detail::attach(f.song, f.clip, kit, params);
	f.instrument = kit;
	return f;
}

/// @brief Give a kit fixture one drum with a note row, and select it -- the model half of
///        `InstrumentClipView::createDrumForAuditionedNoteRow` (`instrument_clip_view.cpp:5524`).
///
/// `T` is `SoundDrum`, `MIDIDrum` or `GateDrum`. Returns the drum, already `kit->selectedDrum`.
template <typename T>
inline T* add_drum(SongFixture& f) {
	auto* kit = static_cast<Kit*>(f.instrument);

	T* drum = detail::place<T>();

	ParamManagerForTimeline params;
	if constexpr (std::is_same_v<T, SoundDrum>) {
		params.setupWithPatching();
		Sound::initParams(&params);
		drum->setupAsSample(&params);
		f.song->backUpParamManager(drum, f.clip, &params, true);
	}

	kit->addDrum(drum);

	// One note row per drum, as the kit's own reader does; `setDrum` copies the backed-up param
	// manager onto the row (which is what `setup()`'s SoundDrum branch then reads).
	char stack_memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(stack_memory, f.song, f.clip);
	int32_t row_index = 0;
	NoteRow* row = f.clip->createNewNoteRowForKit(stack, /*atStart=*/false, &row_index);
	ModelStackWithNoteRow* row_stack = stack->addNoteRow(row_index, row);
	row->setDrum(drum, kit, row_stack);

	kit->selectedDrum = drum;
	return drum;
}

} // namespace deluge::scenes::spec
