// What the marker editor's two surfaces show, as the plain structs the views draw. Const members: nothing here writes
// the scene or reads a frame.
#include "base/divide.h"
#include "project/device_settings.h"
#include "project/sample_marker.h"
#include "ui/gfx/grid/palette.h"
#include "ui/scenes/marker/marker_scene.h"
#include "ui/scenes/marker/pins.h"
#include "ui/scenes/services/waveform_paint.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::ui::scenes::marker {

namespace {

using deluge::project::kSampleMarkerCount;
using deluge::project::SampleMarker;

static_assert(kSampleMarkerCount == deluge::ui::widgets::grid::kMaxMarkers);

} // namespace

views::oled::MarkerPosition MarkerScene::present_position(const bind::WaveformInfo& info) const {
	const std::array<views::waveform::MarkerPin, kSampleMarkerCount> cols = pins();

	// `renderOLED`'s title switch, through `reverseRemap` -- so a reversed sample's
	// START marker is labelled "End point".
	std::string_view label;
	switch (reverse_remap(marker_type_)) {
	case SampleMarker::START:
		label = "Start point";
		break;
	case SampleMarker::END:
		label = "End point";
		break;
	case SampleMarker::LOOP_START:
		label = "Loop start";
		break;
	case SampleMarker::LOOP_END:
		label = "Loop end";
		break;
	default:
		// Legacy's `default: __builtin_unreachable()`, made survivable: `markerType` is only ever
		// one of the four, but an unreachable here would be a crash rather than a blank title if it
		// ever were not.
		label = "";
		break;
	}

	// Literals, not l10n ids, because none of the four is in `gui/l10n/strings.h` -- exactly
	// `BrowserScene::Descriptor::title`'s situation and answer.
	return {.label = label,
	        .loop_locked = loop_locked(),
	        .position = static_cast<uint64_t>(cols[idx(marker_type_)].position),
	        .sample_rate = info.sample_rate};
}

std::array<deluge::ui::widgets::grid::PeakCol, views::waveform::kColumns>
MarkerScene::present_waveform(const services::WaveformFrame& wf, int32_t main_cols) const {
	// `renderMainPads`' `waveformRenderer.renderFullScreen`, scaled by the range after the read: `renderOneCol`'s
	// monochrome arm, as `renderFullScreen` passes no colour. A column the card could not give is invalid, so black.
	return services::peak_cols(wf.read, wf.centre, wf.span, main_cols);
}

views::grid::MarkerOverlay
MarkerScene::present_overlay(const std::array<views::waveform::MarkerPin, kSampleMarkerCount>& cols,
                             int32_t main_cols) const {
	// The blink, as `timerCallback` draws it rather than as `renderMainPads`
	// does -- see the class docstring for why the two disagree.
	uint32_t suppress = 0;
	if (blink_phase_ == 3) {
		suppress |= 1u << idx(marker_type_);
		if (loop_locked()) {
			if (marker_type_ == SampleMarker::LOOP_START) {
				suppress |= 1u << idx(SampleMarker::LOOP_END);
			}
			if (marker_type_ == SampleMarker::LOOP_END) {
				suppress |= 1u << idx(SampleMarker::LOOP_START);
			}
		}
	}

	const int64_t selected_col = column_or_far_left(cols[idx(marker_type_)]);

	views::grid::MarkerOverlay d{
	    .active = {}, .suppress = suppress, .selected = idx(marker_type_), .colours = {}, .flood = -1};
	// Colours are resolved here rather than in the widget: `reverseRemap` reads
	// `SampleControls`, which `src/ui` may not include.
	for (int32_t m = 0; m < static_cast<int32_t>(kSampleMarkerCount); m++) {
		d.colours[static_cast<size_t>(m)] =
		    marker_colours()[static_cast<size_t>(idx(reverse_remap(static_cast<SampleMarker>(m))))];
	}
	const int32_t shown = std::min<int32_t>(main_cols, static_cast<int32_t>(d.active.size()));
	for (int32_t col = 0; col < shown; col++) {
		if (col == selected_col && blink_phase_ == 1) {
			// Phase 1: the whole column flashes the selected marker's colour.
			d.flood = col;
			continue;
		}
		for (int32_t m = 0; m < static_cast<int32_t>(kSampleMarkerCount); m++) {
			if (column_or_far_left(cols[m]) == col) {
				d.active[static_cast<size_t>(col)] |= 1u << m;
			}
		}
	}
	return d;
}

views::grid::Playhead MarkerScene::present_playhead(int32_t main_cols, std::optional<int64_t> pos) const {
	// `graphicsRoutine`'s playhead: nothing with the flash cursor Off; else the column the newest voice
	// plays, on every row, painted with it Slow and flashed by the PIC with it Fast (`PadLEDs::setTickSquares`).
	const deluge::project::FlashCursor cursor = play_cursor();
	views::grid::Playhead d{.per_row = {},
	                        .paint = cursor == deluge::project::FlashCursor::SLOW,
	                        .muted = deluge::gui::colours::black,
	                        .flash = cursor != deluge::project::FlashCursor::OFF,
	                        .fast = cursor == deluge::project::FlashCursor::FAST};
	if (!d.flash) {
		return d;
	}
	if (pos.has_value() && *pos >= view_.scroll()) {
		const int64_t col = base::floor_div(*pos - view_.scroll(), view_.zoom());
		if (col < main_cols) {
			d.per_row.fill(deluge::ui::widgets::grid::TickSquare{
			    .col = static_cast<uint8_t>(col), .colour = deluge::ui::widgets::grid::TickColour::Normal});
		}
	}
	return d;
}

} // namespace deluge::ui::scenes::marker
