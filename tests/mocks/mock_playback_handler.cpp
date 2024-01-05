#include "deluge/playback/playback_handler.h"

PlaybackHandler playbackHandler{};

PlaybackHandler::PlaybackHandler() {
	// XXX: This is just copied from the main playback handler, but we should probably have an IPlaybackHandler or
	// something and swap that out for tests
	tapTempoNumPresses = 0;
	playbackState = 0;
	analogInTicksPPQN = 24;
	analogOutTicksPPQN = 24;
	analogClockInputAutoStart = true;
	metronomeOn = false;
	midiOutClockEnabled = true;
	midiInClockEnabled = true;
	tempoMagnitudeMatchingEnabled = false;
	posToNextContinuePlaybackFrom = 0;
	stopOutputRecordingAtLoopEnd = false;
	recording = RECORDING_OFF;
	countInEnabled = true;
	timeLastMIDIStartOrContinueMessageSent = 0;
	currentVisualCountForCountIn = 0;
}

uint64_t PlaybackHandler::getTimePerInternalTickBig() {
	// TODO: add an interface for this, so tests can manipulate it
	return 100;
}
