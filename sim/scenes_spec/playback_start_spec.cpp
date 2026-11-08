/// @file
/// @brief Starting playback against a render that preempts the start-up.
///
/// On the device the render runs on the audio tier, which preempts the interaction tier wherever it is, so a block can
/// land inside `PlaybackHandler::setupPlaybackUsingInternalClock`, after `setupPlayback` has published
/// `playbackState` and before the clock's timebase (`timeNextTimerTickBig`) is set. The render must not tick the clock
/// there: from the old timebase -- 0 on the first start after boot -- it would action every timer tick since it in one
/// block. The harness hook `deluge::playback::renderPreemptsStartupForHarness` stands in for that block and runs the
/// render's sequencer step, `AudioEngine::tickSongFinalizeWindows`, at the point the device's render can land.

#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"

#include "support/card_access.h" // before <cppspec.hpp> -- see its @warning
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <cstddef>
#include <cstdint>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;

namespace {

/// The sample time playback starts at: ten seconds after boot.
constexpr uint32_t kStartSample = 10 * 44100;

/// The block a render runs.
constexpr size_t kBlockFrames = 128;

int32_t g_preemptions = 0;
int64_t g_ticksScheduledInBlock = -1;

/// The render block the device can run where `setupPlayback` has published `playbackState`.
void render_block_in_startup() {
	g_preemptions++;
	size_t frames = kBlockFrames;
	AudioEngine::tickSongFinalizeWindows(frames);
	g_ticksScheduledInBlock = playbackHandler.nextTimerTickScheduled;
}

/// Stopped, at kStartSample, with the timebase a first start after boot finds.
void reset_to_stopped() {
	playbackHandler.playbackState = 0;
	playbackHandler.recording = RecordingMode::OFF;
	AudioEngine::audioSampleTimer = kStartSample;
	playbackHandler.timeNextTimerTickBig = 0;
	playbackHandler.timeLastTimerTickBig = 0;
	playbackHandler.lastTimerTickActioned = 0;
	playbackHandler.nextTimerTickScheduled = 0;
	g_preemptions = 0;
	g_ticksScheduledInBlock = -1;
}

} // namespace

// clang-format off
describe playback_start_spec_body("PlaybackHandler start-up under a preempting render", $ {

	it("a render block that lands while internal-clock playback starts actions no timer tick", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		expect(fixture.song != nullptr).to_be_true();
		currentSong = fixture.song;
		spec::QuietAudio quiet;
		reset_to_stopped();

		deluge::playback::renderPreemptsStartupForHarness = render_block_in_startup;
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
		deluge::playback::renderPreemptsStartupForHarness = nullptr;

		expect(g_preemptions).to_equal(1);
		// No tick was actioned in the block, so none was scheduled past tick 0.
		expect(g_ticksScheduledInBlock).to_equal(int64_t{0});

		// The first block after the start-up actions tick 0, at the start time.
		size_t frames = kBlockFrames;
		AudioEngine::tickSongFinalizeWindows(frames);
		expect(playbackHandler.lastTimerTickActioned).to_equal(int64_t{0});
		expect(static_cast<uint32_t>(playbackHandler.timeLastTimerTickBig >> 32)).to_equal(kStartSample);

		playbackHandler.endPlayback();
	});
});
// clang-format on

DELUGE_SPEC(playback_start_spec, playback_start_spec_body)
