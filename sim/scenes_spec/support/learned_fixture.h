#pragma once

/// @file
/// @brief The song-building and transport helpers the learned-dispatch specs share: outputs with clips, a kit's rows,
///        the clock running, a step region held, a root UI for an undo step to open under.

#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "model/fx/chain_params.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include "support/song_fixture.h"

namespace deluge::scenes::spec::learned {

/// @brief A view as the root UI, so an undo step can open, for one example.
struct RootView {
	RootView() { changeRootUI(&sessionView); }
	~RootView() { nullifyUIs(); }
	RootView(const RootView&) = delete;
	RootView& operator=(const RootView&) = delete;
};

/// @brief The clock running, with @p mode recording, for as long as it lives.
struct Transport {
	RecordingMode saved_mode = playbackHandler.recording;
	decltype(playbackHandler.playbackState) saved_state = playbackHandler.playbackState;
	explicit Transport(RecordingMode mode) {
		playbackHandler.recording = mode;
		playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
	}
	~Transport() {
		playbackHandler.recording = saved_mode;
		playbackHandler.playbackState = saved_state;
	}
	Transport(const Transport&) = delete;
	Transport& operator=(const Transport&) = delete;
};

/// @brief The step-edit region [@p pos, @p pos + @p length) held on @p clip, for as long as it lives.
struct HeldRegion {
	HeldRegion(Clip& clip, uint32_t pos, uint32_t length) {
		view.setActiveModControllableTimelineCounter(&clip, false);
		view.setModRegion(pos, length, 0);
	}
	~HeldRegion() {
		view.setModRegion();
		view.releaseActiveModControllable();
	}
	HeldRegion(const HeldRegion&) = delete;
	HeldRegion& operator=(const HeldRegion&) = delete;
};

/// @brief Make @p clip its @p output's active clip, as a song's own load does.
inline void activate(Song& song, Output& output, Clip& clip) {
	alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
	(void)output.setActiveClip(setupModelStackWithSong(memory, &song)->addTimelineCounter(&clip), PgmChangeSend::NEVER);
}

/// @brief A synth built the way a preset load builds one, its params backed up with the song, not yet in it.
inline SoundInstrument* makeLooseSynth(Song& song) {
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->setupAsDefaultSynth(&params);
	song.live.param_backup.backUp(synth, nullptr, &params, true);
	return synth;
}

/// @brief A synth with a session clip of its own, in @p song.
inline SongFixture addSynth(Song* song) {
	SongFixture f{.song = song, .clip = detail::make_session_clip(song)};
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->setupAsDefaultSynth(&params);
	detail::attach(song, f.clip, synth, params);
	f.instrument = synth;
	return f;
}

/// @brief A kit with no rows and a session clip of its own, in @p song.
inline SongFixture addKit(Song* song) {
	SongFixture f{.song = song, .clip = detail::make_session_clip(song)};
	ParamManagerForTimeline params;
	params.setupUnpatched();
	deluge::model::initGlobalParams(&params);
	auto* kit = static_cast<Kit*>(StorageManager::createNewInstrument(OutputType::KIT, &params));
	kit->dirPath = "KITS";
	kit->name = "0";
	detail::attach(song, f.clip, kit, params);
	f.instrument = kit;
	return f;
}

/// @brief A sample drum made for @p f's kit as a kit-row preset makes one, its params backed up on the kit's clip,
/// not yet in the kit.
inline SoundDrum* makeSoundDrum(SongFixture& f) {
	auto* drum = detail::place<SoundDrum>();
	ParamManagerForTimeline params;
	params.setupWithPatching();
	Sound::initParams(&params);
	drum->setupAsSample(&params);
	f.song->live.param_backup.backUp(drum, f.clip, &params, true);
	return drum;
}

/// @brief A sound drum added to @p f's kit, with its params backed up on the kit's clip, and no row yet.
inline SoundDrum* addSoundDrum(SongFixture& f) {
	SoundDrum* drum = makeSoundDrum(f);
	static_cast<Kit*>(f.instrument)->addDrum(drum);
	return drum;
}

/// @brief A drum of type @p T added to @p f's kit, with no row yet.
template <typename T>
inline T* addPlainDrum(SongFixture& f) {
	auto* drum = detail::place<T>();
	static_cast<Kit*>(f.instrument)->addDrum(drum);
	return drum;
}

/// @brief A row for @p drum at the end of @p f's kit clip.
inline void addRow(SongFixture& f, Drum* drum) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
	int32_t index = 0;
	NoteRow* row = f.clip->createNewNoteRowForKit(stack, /*atStart=*/false, &index);
	row->setDrum(drum, static_cast<Kit*>(f.instrument), stack->addNoteRow(index, row));
}

} // namespace deluge::scenes::spec::learned
