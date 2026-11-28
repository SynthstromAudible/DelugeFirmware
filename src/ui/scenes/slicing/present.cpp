// What the slicer's two surfaces show, as the plain structs the views draw. Const members: nothing here writes the
// scene or reads a frame.
#include "base/divide.h"
#include "project/device_settings.h"
#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/services/waveform_paint.h"
#include "ui/scenes/slicing/slicer_scene.h"
#include "ui/widgets/oled/detail/int_to_string.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>

namespace deluge::ui::scenes::slicing {

namespace {

constexpr int32_t kMainCols = deluge::ui::gfx::grid::kDelugeGrid.main_cols;
constexpr int32_t kRows = deluge::ui::gfx::grid::kDelugeGrid.rows;

/// `int32_t x = manualSlicePoints[i].startPos / (sample->lengthInSamples + 0.0) * 16`.
/// The 16 is legacy's own literal, not `kDisplayWidth`, and the arithmetic is deliberately in
/// `double` -- `+ 0.0` is what makes it so -- before truncating to `int32_t`.
constexpr int32_t kSliceMarkColumns = 16;

static_assert(kMaxManualSlices == std::tuple_size_v<decltype(views::grid::SliceMarks::cols)>);

/// `Slicer::renderOLED`'s label (`origin/main:src/deluge/gui/ui/slicer.cpp`). A raw literal in legacy, with no entry in
/// `gui/l10n/strings.h`, so it stays one here -- the same call `MarkerScene` makes for its four
/// page titles.
constexpr std::string_view kNumSlicesLabel = "Num. slices";

} // namespace

views::oled::SlicerCount SlicerScene::present_count(std::span<char, 12> digits) const {
	// `intToString(slicerMode == SLICER_MODE_REGION ? numClips : numManualSlice, buffer)`, into legacy's own 12-byte
	// buffer.
	const int32_t value = (mode_ == Mode::Region) ? num_clips_ : num_manual_slices_;
	return {.label = kNumSlicesLabel,
	        .digits = deluge::ui::widgets::oled::detail::int_to_string(value, 1, std::span<char>{digits})};
}

std::array<deluge::ui::widgets::grid::PeakCol, views::waveform::kColumns>
SlicerScene::present_waveform(const services::WaveformFrame& wf) const {
	// `renderOneCol`'s monochrome arm, as `renderFullScreen` passes no colour. A column the card could not give is
	// invalid, so black.
	return services::peak_cols(wf.read, wf.centre, wf.span, kMainCols);
}

views::grid::SliceMarks SlicerScene::present_marks() const {
	views::grid::SliceMarks d{
	    .manual = mode_ == Mode::Manual, .count = num_manual_slices_, .current = current_slice_, .cols = {}};
	// The column is `startPos / lengthInSamples * 16` in double arithmetic, so a slice at the very end of the sample
	// lands on column 16 -- the first SIDEBAR column in legacy's image. The view's region clips it: that is the
	// behaviour-preserving choice rather than a transcription of the write.
	const int32_t placed = std::min<int32_t>(num_manual_slices_, kMaxManualSlices);
	for (int32_t i = 0; i < placed; i++) {
		d.cols[static_cast<size_t>(i)] = static_cast<int32_t>(
		    points_[i].start_pos / (static_cast<double>(view_.length()) + 0.0) * kSliceMarkColumns);
	}
	return d;
}

views::grid::Playhead SlicerScene::present_playhead(std::optional<int32_t> pos) const {
	// `graphicsRoutine`'s playhead: the newest voice's column on rows 7 to 4, in both modes, handed to
	// `PadLEDs::setTickSquares` whatever the flash cursor: painted with it Slow, flashed by the PIC with it Fast.
	// The device's play cursor is Slow, the row's default, with no device port.
	const deluge::project::FlashCursor cursor = bind::read_device<deluge::project::FlashCursor>(
	    bind::port_of(ports_, &bind::Ports::device), deluge::project::device_path::kPlayCursor);
	views::grid::Playhead d{.per_row = {},
	                        .paint = cursor == deluge::project::FlashCursor::SLOW,
	                        .muted = deluge::gui::colours::black,
	                        .flash = true,
	                        .fast = cursor == deluge::project::FlashCursor::FAST};
	if (pos.has_value() && *pos >= view_.scroll()) {
		const int64_t col = base::floor_div(*pos - view_.scroll(), view_.zoom());
		if (col < kMainCols) {
			for (int32_t y = kRows - 4; y < kRows; y++) {
				d.per_row[static_cast<size_t>(y)] = deluge::ui::widgets::grid::TickSquare{
				    .col = static_cast<uint8_t>(col), .colour = deluge::ui::widgets::grid::TickColour::Normal};
			}
		}
	}
	return d;
}

} // namespace deluge::ui::scenes::slicing
