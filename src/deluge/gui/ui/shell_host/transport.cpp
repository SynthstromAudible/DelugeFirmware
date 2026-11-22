#include "gui/ui/shell_host/shell_host.h"
#include "model/action/action_logger.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"

#include <utility>

namespace deluge::gui::shell_host {
namespace {

static_assert(static_cast<int>(RecordingMode::OFF)
              == std::to_underlying(deluge::ui::scenes::bind::TransportRecording::OFF));
static_assert(static_cast<int>(RecordingMode::NORMAL)
              == std::to_underlying(deluge::ui::scenes::bind::TransportRecording::NORMAL));
static_assert(static_cast<int>(RecordingMode::ARRANGEMENT)
              == std::to_underlying(deluge::ui::scenes::bind::TransportRecording::ARRANGEMENT));

/// The transport's state, read from `playbackHandler`, `currentPlaybackMode`, `actionLogger` and the current song.
class HostTransport final : public deluge::ui::scenes::bind::Transport {
public:
	[[nodiscard]] bool playing() const override { return playbackHandler.playbackState != 0; }
	[[nodiscard]] bool clocks_active() const override { return playbackHandler.isEitherClockActive(); }
	[[nodiscard]] deluge::ui::scenes::bind::TransportRecording recording() const override {
		using deluge::ui::scenes::bind::TransportRecording;
		switch (playbackHandler.recording) {
		case RecordingMode::OFF:
			return TransportRecording::OFF;
		case RecordingMode::NORMAL:
			return TransportRecording::NORMAL;
		case RecordingMode::ARRANGEMENT:
			return TransportRecording::ARRANGEMENT;
		}
		return TransportRecording::OFF;
	}
	[[nodiscard]] bool metronome_on() const override { return playbackHandler.metronomeOn; }
	[[nodiscard]] bool undo_allowed() const override { return actionLogger.allowedToDoReversion(); }
	[[nodiscard]] bool overdubs_pending() const override {
		return currentSong != nullptr && currentSong->hasAnyPendingNextOverdubs();
	}
	[[nodiscard]] int32_t count_in_visual() const override { return playbackHandler.count_in_visual(); }
	[[nodiscard]] bool arrangement_mode() const override { return currentPlaybackMode == &arrangement; }
};

} // namespace

const deluge::ui::scenes::bind::Transport& transport() {
	static const HostTransport state;
	return state;
}

} // namespace deluge::gui::shell_host
