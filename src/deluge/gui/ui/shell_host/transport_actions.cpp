#include "definitions_cxx.hpp"
#include "gui/ui/shell_host/shell_host.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/recording/foreground_recording.h"

#include <cstdint>

// Defined in playback/playback_handler.cpp with no declaration in its header; declared here with their defining types.
extern GlobalMIDICommand pendingGlobalMIDICommand;
extern int32_t pendingGlobalMIDICommandNumClustersWritten;

namespace deluge::gui::shell_host {
namespace {

/// The transport's actions, each the call the legacy button handling makes, popups included. PLAY, RECORD, the tempo
/// encoder and the output recording are `Buttons::buttonAction`'s (`origin/main:src/deluge/hid/buttons.cpp`); tap
/// tempo, the metronome, undo and fill are `View::buttonAction`'s (`origin/main:src/deluge/gui/views/view.cpp`).
class HostTransportActions final : public scenes::bind::TransportActions {
public:
	void play() override { playbackHandler.playButtonPressed(kInternalButtonPressLatency); }
	void record() override { playbackHandler.recordButtonPressed(); }
	void tap_tempo(bool normal) override { playbackHandler.tapTempoButtonPress(normal); }
	void toggle_metronome() override { playbackHandler.toggleMetronomeStatus(); }
	void clear_tempo_automation() override { playbackHandler.commandClearTempoAutomation(); }
	void show_swing_interval() override { playbackHandler.commandDisplaySwingInterval(); }
	void show_tempo() override { playbackHandler.commandDisplayTempo(); }
	void undo(bool redo) override {
		// `View::buttonAction`'s BACK arm (`origin/main:src/deluge/gui/views/view.cpp`): the pending command, then the
		// routine that runs it at once, inside the button's handling.
		pendingGlobalMIDICommand = redo ? GlobalMIDICommand::REDO : GlobalMIDICommand::UNDO;
		// `View::buttonAction` writes `GlobalMIDICommand::PLAYBACK_RESTART` through an `extern GlobalMIDICommand`
		// declaration of this `int32_t`; that enumerator is 0 and both types are `int`-sized, so 0 is the same bits.
		pendingGlobalMIDICommandNumClustersWritten = 0;
		playbackHandler.slowRoutine();
	}
	void begin_output_recording() override { (void)deluge::recording::beginOutputRecording(); }
	void end_recording_soon() override { deluge::recording::endRecordingSoon(kInternalButtonPressLatency); }
	void stop_output_recording_at_loop_end() override {
		if (currentPlaybackMode != nullptr) {
			currentPlaybackMode->stopOutputRecordingAtLoopEnd();
		}
	}
	void fill(bool on) override {
		if (currentSong != nullptr) {
			currentSong->changeFillMode(on);
		}
	}
};

} // namespace

scenes::bind::TransportActions& transport_actions() {
	static HostTransportActions actions;
	return actions;
}

} // namespace deluge::gui::shell_host
