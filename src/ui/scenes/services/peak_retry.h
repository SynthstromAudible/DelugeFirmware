#pragma once

/// @file
/// @brief When a waveform view whose last read left columns pending draws again.

#include "ui/views/waveform/peaks.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::scenes::services {

/// @brief How soon a view with pending columns draws again, in ms: one tick of the UI loop.
inline constexpr uint32_t kPeakRetryPollMs = 10;
/// @brief How long a view keeps drawing again with no progress (no column newly answered, its pending columns' index
///        no fuller), in ms: the recorder's bound on an
///        unproductive card wait (`kMaxUnproductiveUs`, `model/sample/recorder_write_policy.h`), past which the card
///        is stuck rather than slow.
inline constexpr uint32_t kPeakRetryGiveUpMs = 300;

/// @brief One view's redraws of a waveform whose columns are pending: again every kPeakRetryPollMs while a read makes
///        progress within kPeakRetryGiveUpMs, then not until a read answers one more column. A read makes progress
///        when it answers more columns or its pending columns' index has folded more of their frames
///        (PeakRead::progress): a column spanning many clusters stays pending while its index fills. A view that never
///        gives up asks again every kPeakRetryPollMs for as long as a column is pending. A frame read at another window
///        than the last starts over: a new view counts its own columns.
class PeakRetry {
public:
	/// @brief Whether a view stops drawing again when the card stalls.
	enum class Patience : uint8_t {
		GIVES_UP,       ///< stop after kPeakRetryGiveUpMs with no progress
		UNTIL_ANSWERED, ///< ask again every kPeakRetryPollMs until no column is pending
	};
	/// @brief What a view does after a frame's read.
	enum class Next : uint8_t {
		DONE,      ///< nothing pending: no redraw for the waveform
		ASK_AGAIN, ///< draw again in kPeakRetryPollMs
		GIVE_UP,   ///< stop drawing again; this frame is the one that stops
		WAIT,      ///< still stopped: no redraw until a read answers one more column
	};
	/// @brief A view's redraws, giving up by @p patience.
	constexpr explicit PeakRetry(Patience patience = Patience::GIVES_UP) : patience_{patience} {}
	/// @brief Start over, for a new waveform or a new showing of it.
	constexpr void restart() {
		best_.reset();
		known_ = 0;
		given_up_ = false;
		window_.reset();
	}
	/// @brief Whether a redraw is under way: a read left columns pending since the last restart or complete read.
	[[nodiscard]] constexpr bool retrying() const { return best_.has_value(); }
	/// @brief Whether a redraw of @p window is under way: a frame read at another window starts over, so a redraw of
	///        the last window does not carry to it.
	[[nodiscard]] constexpr bool retrying(views::waveform::Window window) const {
		return best_.has_value() && window_ == window;
	}
	/// @brief Whether it stopped drawing again.
	[[nodiscard]] constexpr bool gave_up() const { return given_up_; }
	/// @brief Judge one frame's read of @p window at @p now_ms.
	[[nodiscard]] constexpr Next after(const views::waveform::PeakRead& read, views::waveform::Window window,
	                                   uint32_t now_ms) {
		if (window_ != window) {
			restart();
			window_ = window;
		}
		if (!read.pending()) {
			best_.reset();
			known_ = 0;
			given_up_ = false;
			return Next::DONE;
		}
		if (patience_ == Patience::UNTIL_ANSWERED) {
			best_ = read.answered();
			return Next::ASK_AGAIN;
		}
		const int32_t answered = read.answered();
		// An answered column's frames leave the pending sum, so a newly answered column restarts the sum's baseline.
		if (!best_ || answered > *best_) {
			best_ = answered;
			known_ = read.progress();
			progress_ms_ = now_ms;
		}
		else if (read.progress() > known_) {
			known_ = read.progress();
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
	std::optional<int32_t> best_;                   ///< the most columns a read has answered since the restart
	uint64_t known_{0};                             ///< the most pending frames folded since that count last rose
	uint32_t progress_ms_{0};                       ///< when either last rose
	std::optional<views::waveform::Window> window_; ///< the window the last frame read
	Patience patience_;
	bool given_up_{false};
};

} // namespace deluge::ui::scenes::services
