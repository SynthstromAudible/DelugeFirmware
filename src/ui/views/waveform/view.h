#pragma once

/// @file
/// @brief Scroll and zoom over a waveform that no timeline drives: a sample's markers, its slicer and its preview.

#include "base/divide.h"
#include "ui/views/waveform/peaks.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace deluge::ui::views::waveform {

/// @brief A marker a zoom may pin in place: where it is, and the column it lands on now.
struct MarkerPin {
	int64_t position{0};           ///< the marker's frame
	std::optional<int64_t> column; ///< the column it lands on now, on screen or off; none when it is not set
	bool right_edge{false};        ///< it pins by its column's right edge: an end marker names its last frame's column
	bool operator==(const MarkerPin&) const = default;
};

/// @brief Which part of a waveform the kColumns columns show.
class WaveformView {
public:
	/// @brief The view of an empty waveform: zoom 1 from the start.
	constexpr WaveformView() = default;
	/// @brief A view opened on a waveform of @p length frames: at @p remembered, with its zoom clamped into
	///        [1, max_zoom()] and its scroll inside the waveform; or, with none or a zoom of 0 remembered, zoomed all
	///        the way out from the start.
	/// @param length     The waveform's frames, at most 2^62: then a view's frames and the column past it, 17 zooms
	///                   from its scroll, fit in an `int64_t`.
	/// @param remembered The view last shown on this waveform; none for a waveform not shown before.
	/// @return The view.
	[[nodiscard]] static constexpr WaveformView open(uint64_t length, std::optional<Window> remembered);
	[[nodiscard]] constexpr uint64_t length() const { return length_; } ///< frames
	[[nodiscard]] constexpr int64_t scroll() const { return scroll_; }  ///< column 0's first frame
	[[nodiscard]] constexpr int64_t zoom() const { return zoom_; }      ///< frames per column, at least 1
	/// @brief The window the view's columns cover.
	[[nodiscard]] constexpr Window window() const { return {.scroll = scroll_, .zoom = zoom_}; }
	/// @brief The same view over a waveform now @p length frames long.
	/// @param length The waveform's frames, at most 2^62 (as for open()).
	/// @return The view, at the same scroll and zoom.
	[[nodiscard]] constexpr WaveformView with_length(uint64_t length) const { return {length, scroll_, zoom_}; }
	/// @brief The zoom at which the whole waveform fits: `ceil(length / kColumns)`, and 1 for an empty waveform.
	[[nodiscard]] constexpr int64_t max_zoom() const;
	/// @brief Whether the view shows less than the whole waveform.
	[[nodiscard]] constexpr bool zoomed_in() const { return zoom_ < max_zoom(); }
	/// @brief The view one zoom step in (@p offset >= 0) or out. With a preferred pin, it pins the preferred pin when
	///        that is on screen, else the on-screen pin whose pin point is nearest the centre, else the centre. A pin's
	///        pin point is its column, or the column after it for a right-edge pin.
	/// @param offset      The step's direction.
	/// @param pins        The zone's markers in order: start, loop start, loop end, end.
	/// @param preferred   The pin to prefer, or none to pin the centre.
	/// @param extra_right Whether the view may scroll past the waveform's end.
	/// @return The new view; none at the zoom's limit.
	[[nodiscard]] constexpr std::optional<WaveformView> zoomed(int32_t offset, std::span<const MarkerPin> pins,
	                                                           std::optional<size_t> preferred, bool extra_right) const;
	/// @brief The view one column right (@p offset >= 0) or left, never before the start.
	/// @param offset      The step's direction.
	/// @param extra_right Whether the view may scroll past the waveform's end, as far as frames go.
	/// @param end_column  The end marker's column; none when no marker is shown, or the end is not set. Without
	///                    @p extra_right, the view moves past the waveform's end only to reach an end marker off
	///                    screen to the right.
	/// @return The new view; none where the scroll is refused.
	[[nodiscard]] constexpr std::optional<WaveformView> scrolled(int32_t offset, bool extra_right,
	                                                             std::optional<int64_t> end_column) const;
	/// @brief The view moved @p columns whole columns, never before the start.
	/// @param columns How many columns to move: right when positive. The moved view's end, one past its last frame,
	///                fits in an `int64_t`.
	/// @return The moved view.
	[[nodiscard]] constexpr WaveformView moved(int32_t columns) const {
		return {length_, std::max<int64_t>(0, scroll_ + zoom_ * columns), zoom_};
	}
	/// @brief The view with its scroll clamped inside the waveform: never before the start and, without
	///        @p extra_right, never so far right that the last column passes the waveform's last whole column. A
	///        waveform narrower than the view starts at column 0.
	/// @param extra_right Whether the view may scroll past the waveform's end.
	/// @return The clamped view.
	[[nodiscard]] constexpr WaveformView clamped(bool extra_right) const;
	bool operator==(const WaveformView&) const = default;

private:
	constexpr WaveformView(uint64_t length, int64_t scroll, int64_t zoom)
	    : length_{length}, scroll_{scroll}, zoom_{zoom} {}
	uint64_t length_{0};
	int64_t scroll_{0};
	int64_t zoom_{1};
};

/// @brief A view one scene hands another, so the other shows a file as it was last drawn: the file and the view. The
///        columns are the waveforms port's, keyed by the same file.
struct ShownWaveform {
	uint64_t identity{0}; ///< the file's identity, as the waveforms port's info gives it
	WaveformView view{};  ///< the view it was drawn at
	bool operator==(const ShownWaveform&) const = default;
};

constexpr int64_t WaveformView::max_zoom() const {
	const uint64_t whole = (length_ >> kColumnsLog2) + ((length_ & static_cast<uint64_t>(kColumns - 1)) != 0 ? 1 : 0);
	return std::max<int64_t>(1, static_cast<int64_t>(whole));
}

constexpr WaveformView WaveformView::open(uint64_t length, std::optional<Window> remembered) {
	const int64_t whole = WaveformView{length, 0, 1}.max_zoom();
	if (!remembered || remembered->zoom == 0) {
		return {length, 0, whole};
	}
	return WaveformView{length, remembered->scroll, std::clamp<int64_t>(remembered->zoom, 1, whole)}.clamped(false);
}

constexpr std::optional<WaveformView> WaveformView::zoomed(int32_t offset, std::span<const MarkerPin> pins,
                                                           std::optional<size_t> preferred, bool extra_right) const {
	int64_t new_zoom = 0;
	if (offset >= 0) {
		if (zoom_ < 2) {
			return std::nullopt;
		}
		const auto zoom = static_cast<uint64_t>(zoom_);
		if (std::has_single_bit(zoom)) {
			new_zoom = zoom_ >> 1;
		}
		else {
			// Between two powers of two: the upper one's half from 0.707 of it up, its quarter below.
			const auto next = static_cast<int64_t>(std::bit_ceil(zoom));
			new_zoom = static_cast<double>(zoom_) >= static_cast<double>(next) * 0.707 ? next >> 1 : next >> 2;
		}
	}
	else {
		const int64_t limit = max_zoom();
		if (zoom_ >= limit) {
			return std::nullopt;
		}
		new_zoom = zoom_ << 1;
		if (new_zoom >= limit || static_cast<double>(new_zoom << 1) * 0.707 >= static_cast<double>(limit)) {
			new_zoom = limit;
		}
	}
	constexpr int64_t kCentre = kColumns >> 1;
	const auto from_centre = [](int64_t point) { return point < kCentre ? kCentre - point : point - kCentre; };
	int64_t pin_point = kCentre; // the column boundary the pinned frame keeps
	int64_t pin_frame = scroll_ + zoom_ * kCentre;
	bool pin_right = false;
	if (preferred) {
		std::optional<int64_t> best;
		for (size_t m = 0; m < pins.size(); m++) {
			const MarkerPin& pin = pins[m];
			if (!pin.column || *pin.column < 0 || *pin.column >= kColumns) {
				continue; // off screen, or not set: never pinned
			}
			const int64_t point = *pin.column + (pin.right_edge ? 1 : 0);
			if (m == *preferred || !best || from_centre(point) < from_centre(*best)) {
				best = point;
				pin_frame = pin.position;
				pin_right = pin.right_edge;
			}
			if (m == *preferred) {
				break; // the preferred pin, on screen, wins outright
			}
		}
		if (best) {
			pin_point = *best;
		}
	}
	const int64_t unaligned = pin_frame - new_zoom * pin_point;
	// A right-edge pin keeps its column's right edge on a column boundary, so its scroll rounds up.
	const int64_t scroll =
	    (pin_right ? base::ceil_div(unaligned, new_zoom) : base::floor_div(unaligned, new_zoom)) * new_zoom;
	return WaveformView{length_, scroll, new_zoom}.clamped(extra_right);
}

constexpr std::optional<WaveformView> WaveformView::scrolled(int32_t offset, bool extra_right,
                                                             std::optional<int64_t> end_column) const {
	if (offset >= 0) {
		if (extra_right) {
			if (scroll_ > std::numeric_limits<int64_t>::max() - zoom_ * (kColumns + 1)) {
				return std::nullopt; // the moved view's end, one past its last frame, would not fit in an int64_t
			}
			return WaveformView{length_, scroll_ + zoom_, zoom_};
		}
		const bool at_end = scroll_ + zoom_ * kColumns >= static_cast<int64_t>(length_);
		const bool end_off_right = end_column && *end_column >= kColumns;
		if (at_end && !end_off_right) {
			return std::nullopt;
		}
		return WaveformView{length_, scroll_ + zoom_, zoom_};
	}
	if (scroll_ <= 0) {
		return std::nullopt;
	}
	return WaveformView{length_, std::max<int64_t>(0, scroll_ - zoom_), zoom_};
}

constexpr WaveformView WaveformView::clamped(bool extra_right) const {
	if (scroll_ < 0) {
		return {length_, 0, zoom_};
	}
	if (extra_right) {
		return *this;
	}
	const int64_t columns = base::ceil_div(static_cast<int64_t>(length_), zoom_); // the waveform's columns at this zoom
	const int64_t limit = std::max<int64_t>(0, (columns - kColumns) * zoom_);
	return {length_, std::min(scroll_, limit), zoom_};
}

} // namespace deluge::ui::views::waveform
