#include "CppUTest/TestHarness.h"

#include "deluge/playback/playback_handler.h"

void PlaybackHandler::doSongSwap(bool preservePlayPosition) {
	FAIL("not yet implemented");
}

void PlaybackHandler::endPlayback() {
	FAIL("not yet implemented");
}

void PlaybackHandler::expectEvent() {
	FAIL("not yet implemented");
}

void PlaybackHandler::finishTempolessRecording(bool shouldStartPlaybackAgain, int32_t buttonLatencyForTempolessRecord,
                                               bool shouldExitRecordMode) {
	FAIL("not yet implemented");
}

// This is that special MIDI command for Todd T
void PlaybackHandler::forceResetPlayPos(Song* song) {
	FAIL("not yet implemented");
	if (playbackState) {
		endPlayback();

		if (playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) {
			setupPlaybackUsingExternalClock();
		}

		else {
			setupPlaybackUsingInternalClock();
		}
	}
}
