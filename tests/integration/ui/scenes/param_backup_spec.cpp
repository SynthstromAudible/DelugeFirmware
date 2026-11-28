// The song's param backup, keyed by owner and clip id: an owner's parked params are found by its address once the song
// adopts it, and by the owner itself until then, so a backup made while a song is read (before its ids are final)
// keeps them and moves under the owner's id with the adoption.

#include "model/fx/chain_params.h"
#include "model/project_bridge/param_backup.h"

#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "storage/storage_manager.h"

#include "integration/scenarios/drivers/tree_load.h"
#include "support/popup_capture.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/song_load.h"

#include "support/spec_main.h"

#include <string>
#include <string_view>

namespace {

/// A song with one audio track and no clips, whose saved volume is not the default.
constexpr std::string_view kAudioTrackNoClips = R"(<?xml version="1.0" encoding="UTF-8"?>
<song firmwareVersion="c1.3.0" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" timerTickFraction="-1342177280" inputTickMagnitude="2">
	<instruments>
		<audioTrack name="AUDA">
			<defaultParams volume="0x30000000" />
		</audioTrack>
	</instruments>
	<sessionClips>
	</sessionClips>
</song>
)";

} // namespace

using deluge::project::OwnerId;
namespace params = deluge::modulation::params;

// clang-format off
describe param_backup("the song's param backup, by owner and clip id", ${
	it("an audio track with no clips keeps its saved params through a load", _ {
		Song* const saved = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(kAudioTrackNoClips);
		expect(song).not_().to_be_null();
		if (song != nullptr) {
			Output* audio = song->live.media.first_output;
			expect(audio).not_().to_be_null();
			if (audio != nullptr) {
				expect(audio->type).to_equal(OutputType::AUDIO);
				expect(audio->id().has_value()).to_be_true();
			}
			if (audio != nullptr && audio->id()) {
				ParamManager* params = song->live.param_backup.getPreferablyWithClip(*audio, nullptr);
				expect(params).not_().to_be_null();
				if (params != nullptr) {
					expect(params->getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME))
					    .to_equal(static_cast<int32_t>(0x30000000));
				}
				expect(song->live.param_backup.map.count({OwnerId::instrument(*audio->id()), std::nullopt}))
				    .to_equal(size_t{1});
			}
			expect(song->live.param_backup.pending.empty()).to_be_true();
		}
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("a drum made in the clip view finds its backed-up params under its own id", _ {
		display = &deluge::ui::scenes::spec::quiet_display();
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		NoteRow* row = f.clip->noteRows.insertNoteRowAtIndex(0);
		expect(row).not_().to_be_null();
		if (row != nullptr) {
			alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithNoteRow* stack =
			    setupModelStackWithSong(memory, f.song)->addTimelineCounter(f.clip)->addNoteRow(0, row);
			f.song->setCurrentClip(f.clip);
			SoundDrum* drum = instrumentClipView.createDrumForRow(stack, false);
			expect(drum).not_().to_be_null();
			if (drum != nullptr) {
				expect(row->drum).to_equal(drum);
				expect(drum->id().has_value()).to_be_true();
			}
			expect(row->paramManager.containsAnyMainParamCollections()).to_be_true();
			expect(f.song->live.param_backup.pending.empty()).to_be_true();
		}
		deluge::ui::scenes::spec::make_current(saved);
		display = nullptr;
	});

	it("an owner not yet adopted is backed up by itself, and moves under its id when adopted", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		void* memory = GeneralMemoryAllocator::get().alloc(sizeof(SoundDrum));
		auto* drum = new (memory) SoundDrum();
		ParamManagerForTimeline params;
		expect(params.setupWithPatching()).to_equal(Error::NONE);
		Sound::initParams(&params);
		f.song->live.param_backup.backUp(*drum, nullptr, &params, true);
		expect(f.song->live.param_backup.pending.size()).to_equal(size_t{1});
		expect(f.song->live.param_backup.getPreferablyWithClip(*drum, nullptr)).not_().to_be_null();

		static_cast<Kit*>(f.instrument)->addDrum(drum);
		expect(drum->id().has_value()).to_be_true();
		expect(f.song->live.param_backup.pending.empty()).to_be_true();
		if (drum->id()) {
			expect(f.song->live.param_backup.map.count({OwnerId::drum(*drum->id()), std::nullopt})).to_equal(size_t{1});
		}
		deluge::ui::scenes::spec::make_current(saved);
	});

	it("an audio track the arranger makes in place of another is adopted, its backup under its id", _ {
		deluge::ui::scenes::spec::AudioSongFixture f = deluge::ui::scenes::spec::make_audio_clip_song();
		Song* const saved = currentSong;
		deluge::ui::scenes::spec::make_current(f.song);
		Output* old = f.clip->output;
		AudioOutput* made = f.song->createNewAudioOutput(old);
		expect(made).not_().to_be_null();
		if (made != nullptr) {
			expect(made->docOutput_).not_().to_be_null();
			expect(made->id().has_value()).to_be_true();
			expect(f.song->live.param_backup.pending.empty()).to_be_true();
			if (made->id()) {
				expect(f.song->live.param_backup.map.count({OwnerId::instrument(*made->id()), std::nullopt}))
				    .to_equal(size_t{1});
			}
		}
		deluge::ui::scenes::spec::make_current(saved);
	});
});
// clang-format on

CPPSPEC_SPEC(param_backup)
