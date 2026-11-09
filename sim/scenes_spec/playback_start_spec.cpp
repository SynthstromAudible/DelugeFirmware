/// @file
/// @brief Starting playback against a render that preempts the start-up.
///
/// On the device the render runs on the audio tier, which preempts the interaction tier wherever it is, so a block can
/// land inside `PlaybackHandler::setupPlaybackUsingInternalClock`, after `setupPlayback` has published
/// `playbackState` and before the clock's timebase (`timeNextTimerTickBig`) is set. The render must not tick the clock
/// there: from the old timebase -- 0 on the first start after boot -- it would action every timer tick since it in one
/// block. The render injection at `InjectAt::StartBeforeClock` stands in for that block and runs the render's sequencer
/// step, `AudioEngine::tickSongFinalizeWindows`, at the point the device's render can land.

#include "model/song/song.h"
#include "playback/move_section.h"
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

/// The render block the device can run after `setupPlayback` has positioned the clips and before the clock is
/// published.
void render_block_in_startup() {
	g_preemptions++;
	size_t frames = kBlockFrames;
	AudioEngine::tickSongFinalizeWindows(frames);
	g_ticksScheduledInBlock = playbackHandler.nextTimerTickScheduled;
}

/// What the render saw and scheduled from a block in an external-clock start-up.
struct ExternalBlock {
	bool held = false;
	bool swungTickScheduled = true;
	bool midiClockOutTickScheduled = true;
	bool triggerClockOutTickScheduled = true;
};
ExternalBlock g_external;

/// The render block the device can run after an external-clock start-up's `setupPlayback` and before the clock is
/// published.
void render_block_in_external_startup() {
	g_preemptions++;
	g_external.held = playbackHandler.ticksHeld();
	size_t frames = kBlockFrames;
	AudioEngine::tickSongFinalizeWindows(frames);
	g_external.swungTickScheduled = playbackHandler.swungTickScheduled;
	g_external.midiClockOutTickScheduled = playbackHandler.midiClockOutTickScheduled;
	g_external.triggerClockOutTickScheduled = playbackHandler.triggerClockOutTickScheduled;
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

		deluge::playback::setRenderInjection(deluge::playback::InjectAt::StartBeforeClock, render_block_in_startup);
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
		deluge::playback::setRenderInjection(deluge::playback::InjectAt::StartBeforeClock, nullptr);

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

	it("a render block that lands while external-clock playback starts is held off the clock and schedules nothing", _ {
		static spec::SongFixture fixture = spec::make_synth_song();
		currentSong = fixture.song;
		spec::QuietAudio quiet;
		reset_to_stopped();
		const bool midiOut = playbackHandler.midiOutClockEnabled;
		playbackHandler.midiOutClockEnabled = true; // so the render has a clock-out tick to schedule
		g_external = {};

		deluge::playback::setRenderInjection(deluge::playback::InjectAt::StartBeforeClock,
		                                     render_block_in_external_startup);
		playbackHandler.setupPlaybackUsingExternalClock(false);
		deluge::playback::setRenderInjection(deluge::playback::InjectAt::StartBeforeClock, nullptr);

		expect(g_preemptions).to_equal(1);
		expect(playbackHandler.isExternalClockActive()).to_be_true();
		expect(g_external.held).to_be_true();
		expect(g_external.swungTickScheduled).to_be_false();
		expect(g_external.midiClockOutTickScheduled).to_be_false();
		expect(g_external.triggerClockOutTickScheduled).to_be_false();
		expect(playbackHandler.ticksHeld()).to_be_false();

		playbackHandler.endPlayback();
		playbackHandler.midiOutClockEnabled = midiOut;
	});
});
// clang-format on

DELUGE_SPEC(playback_start_spec, playback_start_spec_body)
