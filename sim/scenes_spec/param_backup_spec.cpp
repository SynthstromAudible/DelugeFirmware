// The song's param backup, keyed by owner and clip id: an owner's parked params are found by its address once the song
// adopts it, and by the owner itself until then, so a backup made while a song is read (before its ids are final)
// keeps them and moves under the owner's id with the adoption.

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
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

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

/// @brief Load @p xml into a new song, as a song file is read; null if the read failed.
Song* load_song(std::string_view xml) {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	std::optional<deluge::storage::Value> tree = parseDocument(xml, deluge::storage::DocumentFormat::XML);
	if (!tree) {
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(deluge::storage::DocumentFormat::XML));
	activeReader = &adapter;
	Error error = openTreeFile(adapter, "song", deluge::storage::DocumentFormat::XML);
	if (error == Error::NONE) {
		error = deluge::bridge::loadSong(*song, adapter);
	}
	activeReader = &smDeserializer;
	return error == Error::NONE ? song : nullptr;
}

/// @brief The minimum `Display` the drum creator's menu and browser opening reach.
struct StubDisplay final : deluge::hid::Display {
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
	void displayNotification(std::string_view, std::optional<std::string_view>) override {}
};

} // namespace

using deluge::project::OwnerId;
namespace params = deluge::modulation::params;

// clang-format off
describe param_backup("the song's param backup, by owner and clip id", ${
	it("an audio track with no clips keeps its saved params through a load", _ {
		Song* const saved = currentSong;
		Song* song = load_song(kAudioTrackNoClips);
		expect(song != nullptr).to_be_true();
		if (song != nullptr) {
			Output* audio = song->live.media.first_output;
			expect(audio != nullptr && audio->type == OutputType::AUDIO && audio->id().has_value()).to_be_true();
			if (audio != nullptr && audio->id()) {
				ParamManager* params = song->live.param_backup.getPreferablyWithClip(*audio, nullptr);
				expect(params != nullptr).to_be_true();
				if (params != nullptr) {
					expect(params->getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME))
					    .to_equal(static_cast<int32_t>(0x30000000));
				}
				expect(song->live.param_backup.map.count({OwnerId::instrument(*audio->id()), std::nullopt}))
				    .to_equal(size_t{1});
			}
			expect(song->live.param_backup.pending.empty()).to_be_true();
		}
		currentSong = saved;
	});

	it("a drum made in the clip view finds its backed-up params under its own id", _ {
		static StubDisplay stub;
		display = &stub;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_kit_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		NoteRow* row = f.clip->noteRows.insertNoteRowAtIndex(0);
		expect(row != nullptr).to_be_true();
		if (row != nullptr) {
			alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithNoteRow* stack =
			    setupModelStackWithSong(memory, f.song)->addTimelineCounter(f.clip)->addNoteRow(0, row);
			f.song->setCurrentClip(f.clip);
			SoundDrum* drum = instrumentClipView.createDrumForRow(stack, false);
			expect(drum != nullptr && row->drum == drum).to_be_true();
			expect(drum != nullptr && drum->id().has_value()).to_be_true();
			expect(row->paramManager.containsAnyMainParamCollections()).to_be_true();
			expect(f.song->live.param_backup.pending.empty()).to_be_true();
		}
		currentSong = saved;
		display = nullptr;
	});

	it("an owner not yet adopted is backed up by itself, and moves under its id when adopted", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_kit_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		void* memory = GeneralMemoryAllocator::get().alloc(sizeof(SoundDrum));
		auto* drum = new (memory) SoundDrum();
		ParamManagerForTimeline params;
		expect(params.setupWithPatching() == Error::NONE).to_be_true();
		Sound::initParams(&params);
		f.song->live.param_backup.backUp(*drum, nullptr, &params, true);
		expect(f.song->live.param_backup.pending.size()).to_equal(size_t{1});
		expect(f.song->live.param_backup.getPreferablyWithClip(*drum, nullptr) != nullptr).to_be_true();

		static_cast<Kit*>(f.instrument)->addDrum(drum);
		expect(drum->id().has_value()).to_be_true();
		expect(f.song->live.param_backup.pending.empty()).to_be_true();
		if (drum->id()) {
			expect(f.song->live.param_backup.map.count({OwnerId::drum(*drum->id()), std::nullopt})).to_equal(size_t{1});
		}
		currentSong = saved;
	});
});
// clang-format on

CPPSPEC_SPEC(param_backup)
