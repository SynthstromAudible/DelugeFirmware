#pragma once

/// @file
/// @brief When a waveform view whose last read left columns pending draws again.

#include "sample-view/peaks.h"

#include <cstdint>
#include <optional>

namespace deluge::scenes::services {

/// @brief How soon a view with pending columns draws again, in ms: one tick of the UI loop.
inline constexpr uint32_t kPeakRetryPollMs = 10;
/// @brief How long a view keeps drawing again with no column newly answered, in ms: the recorder's bound on an
///        unproductive card wait (`kMaxUnproductiveUs`, `model/sample/recorder_write_policy.h`), past which the card
///        is stuck rather than slow.
inline constexpr uint32_t kPeakRetryGiveUpMs = 300;

/// @brief One view's redraws of a waveform whose columns are pending: again every kPeakRetryPollMs while a column is
///        newly answered within kPeakRetryGiveUpMs, then not until a read answers one more.
class PeakRetry {
public:
	/// @brief What a view does after a frame's read.
	enum class Next : uint8_t {
		DONE,      ///< nothing pending: no redraw for the waveform
		ASK_AGAIN, ///< draw again in kPeakRetryPollMs
		GIVE_UP,   ///< stop drawing again; this frame is the one that stops
		WAIT,      ///< still stopped: no redraw until a read answers one more column
	};
	/// @brief Start over, for a new waveform or a new view of it.
	constexpr void restart() {
		best_.reset();
		given_up_ = false;
	}
	/// @brief Whether a redraw is under way: a read left columns pending since the last restart or complete read.
	[[nodiscard]] constexpr bool retrying() const { return best_.has_value(); }
	/// @brief Whether it stopped drawing again.
	[[nodiscard]] constexpr bool gave_up() const { return given_up_; }
	/// @brief Judge one frame's read at @p now_ms.
	[[nodiscard]] constexpr Next after(const sample_view::PeakRead& read, uint32_t now_ms) {
		if (!read.pending()) {
			restart();
			return Next::DONE;
		}
		const int32_t answered = read.answered();
		if (!best_ || answered > *best_) {
			best_ = answered;
			progress_ms_ = now_ms;
		}
		if (static_cast<int32_t>(now_ms - progress_ms_) < static_cast<int32_t>(kPeakRetryGiveUpMs)) {
			return Next::ASK_AGAIN;
		}
		if (!given_up_) {
			given_up_ = true;
			return Next::GIVE_UP;
		}
		return Next::WAIT;
	}

private:
	std::optional<int32_t> best_; ///< the most columns a read has answered since the restart
	uint32_t progress_ms_{0};     ///< when that count last rose
	bool given_up_{false};
};

} // namespace deluge::scenes::services
