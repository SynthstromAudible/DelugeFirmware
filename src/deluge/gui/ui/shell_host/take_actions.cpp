#include "definitions_cxx.hpp"
#include "gui/ui/shell_host/shell_host.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "processing/recording/foreground_recording.h"

#include <atomic>
#include <utility>

namespace deluge::gui::shell_host {
namespace {

/// The input take the recorder screen runs: one recorder, held from `begin` to `finish` or `orphan`.
class HostTakeActions final : public scenes::bind::TakeActions {
public:
	[[nodiscard]] bool begin(scenes::bind::TakeChannels channels) override {
		const bool stereo = channels == scenes::bind::TakeChannels::STEREO;
		const AudioInputChannel mode = stereo ? AudioInputChannel::STEREO : AudioInputChannel::LEFT;
		take_ = AudioEngine::getNewRecorder(stereo ? 2 : 1, AudioRecordingFolder::RECORD, mode, false, false,
		                                    kInternalButtonPressLatency, false, nullptr, {false});
		if (take_ == nullptr) {
			return false;
		}
		take_->allowFileAlterationAfter = true;
		take_->allowNormalization = true;
		// The render keeps measuring the input for the meter after a threshold take triggers.
		take_->monitorInputLevels.store(take_->startValueThreshold > 0, std::memory_order_relaxed);
		// Publishing is what input monitoring follows (`AudioEngine::setMonitoringMode`).
		AudioEngine::setForegroundRecording(take_, mode);
		return true;
	}
	void end_soon() override {
		if (take_ != nullptr) {
			take_->endSyncedRecording(kInternalButtonPressLatency);
		}
	}
	[[nodiscard]] scenes::bind::InputLevels take_levels() override {
		if (take_ == nullptr) {
			return {};
		}
		const auto level = take_->getInputLevels();
		return {.left = level.l, .right = level.r};
	}
	[[nodiscard]] bool take_clipped() override {
		return take_ != nullptr && std::exchange(take_->recordingClippedRecently, false);
	}
	void finish() override {
		if (take_ == nullptr) {
			return;
		}
		take_->pointerHeldElsewhere = false;
		// Unpublished before the discard: `setMonitoringMode`, which runs from the audio routine, dereferences the
		// published recorder, so it must never name one being freed.
		AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE);
		AudioEngine::discardRecorder(take_);
		take_ = nullptr;
	}
	void orphan() override {
		if (take_ == nullptr) {
			return;
		}
		deluge::recording::orphanInputTake();
		take_ = nullptr;
	}

	/// @brief The take held, or null.
	[[nodiscard]] ::SampleRecorder* take() const { return take_; }

private:
	::SampleRecorder* take_{nullptr}; ///< the take held between `begin` and `finish` or `orphan`
};

/// Constant-initialised, so it links as `.data` into SDRAM with the host's other port objects (`sdram_sections.x`).
constinit HostTakeActions host_take_actions;

} // namespace

scenes::bind::TakeActions& take_actions() {
	return host_take_actions;
}

::SampleRecorder* input_take() {
	return host_take_actions.take();
}

} // namespace deluge::gui::shell_host
