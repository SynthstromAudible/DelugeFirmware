/// @file
/// @brief What the audio clip view's two surfaces show, as the plain structs the views draw. Const members: nothing
///        here writes the scene or reads a frame.

#include "ui/scenes/audio_clip/audio_clip_scene.h"

#include "project/song.h"
#include "project/track_kind.h"
#include "ui/gfx/grid/palette.h"
#include "ui/scenes/bind/ports/root_views.h"
#include "ui/scenes/services/global_controls.h"
#include "ui/scenes/timeline/timeline.h"
#include "ui/widgets/grid/columns.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::ui::scenes::audio_clip {

namespace {

/// @brief `renderMainPads`' fixed 16 main columns (`origin/main:src/deluge/gui/views/audio_clip_view.cpp` writes
///        `image[y][0 .. kDisplayWidth)` throughout).
constexpr int32_t kMainCols = deluge::ui::gfx::grid::kDelugeGrid.main_cols;
constexpr int32_t kRows = deluge::ui::gfx::grid::kDelugeGrid.rows;

/// @brief The colour `PadLEDs::prepareColour` substitutes for a `coloursArray` entry of 2, which is
///        the only non-zero value `graphicsRoutine` ever fills
///        (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`).
///
/// `TickColour::Muted` is unreachable from this scene for the same reason, so the `muted_colour`
/// argument `ui::widgets::grid::tick_squares` takes -- legacy's `mutedColourMenu.getRGB()`, a `gui/`
/// menu-item read this layer may not make -- can never be consulted. Black is passed, and this note
/// is why that is not a missing port.
constexpr RGB kUnreachableMutedColour = deluge::gui::colours::black;

} // namespace

std::optional<deluge::ui::views::oled::AudioClipName> AudioClipScene::present_name(NameText& text) const {
	// `AudioClipView::renderOLED` is one call: `view.displayOutputName(getCurrentOutput(), false, getCurrentClip())`
	// (`gui/views/view.cpp`), which flattens the output into five values and hands them to
	// `View::drawOutputNameFromDetails`. `doBlink` is **false** here, so that function's whole first half -- the
	// `IndicatorLED` block -- is skipped.
	const deluge::project::DocClip* record = doc();
	if (record == nullptr || !track()) {
		return std::nullopt;
	}

	// `displayOutputName`'s AUDIO arm (`views/view.cpp`): `channel` is the output's mode, and `editedByUser` stays at
	// its initialiser because the `!= OutputType::AUDIO` branch is not taken. An `AudioClip`'s output is always an
	// `AudioOutput` (`AudioClip::setOutput` is only ever handed one), so the type is `OutputType::AUDIO`.
	const int32_t channel = audio_mode();
	text.name = track_row(deluge::project::SettingGroupId::TRACK, "name");

	// "Section N" when the clip has no name, "N: name" when it has one, with the section counted from one. Formatted by
	// hand -- `deluge::string::fromInt` lives in `util/`, which this layer may not include, and there is no allocation
	// in library code.
	size_t len = 0;
	const auto append = [&](std::string_view s) {
		for (const char c : s) {
			if (len + 1 < text.info.size()) {
				text.info[len++] = c;
			}
		}
	};
	const auto append_int = [&](uint32_t v) {
		std::array<char, 12> digits{};
		size_t n = 0;
		do {
			digits[n++] = static_cast<char>('0' + (v % 10));
			v /= 10;
		} while (v != 0 && n < digits.size());
		while (n > 0) {
			append(std::string_view{&digits[--n], 1});
		}
	};
	const uint32_t section = static_cast<uint32_t>(record->section) + 1;
	if (record->name.empty()) {
		append("Section ");
		append_int(section);
	}
	else {
		append_int(section);
		append(": ");
		append(record->name);
	}
	text.info_len = len;

	return deluge::ui::views::oled::AudioClipName{
	    .type_text = project::track_kind_name(project::TrackKind::AUDIO, channel),
	    .name = text.name,
	    .info = std::string_view{text.info.data(), text.info_len},
	};
}

deluge::ui::views::grid::ClipMarkers AudioClipScene::present_markers() const {
	const int32_t scroll = timeline_.x_scroll;
	const uint32_t zoom = timeline_.x_zoom;
	// `divide_round_negative` is transcribed in `ui/scenes/timeline/timeline.h` because `util/functions.h` is not
	// reachable from this layer.
	return deluge::ui::views::grid::ClipMarkers{
	    .start_square = timeline::divide_round_negative(0 - scroll, static_cast<int32_t>(zoom == 0 ? 1 : zoom)),
	    .end_square =
	        timeline::divide_round_negative(loop_length() - scroll - 1, static_cast<int32_t>(zoom == 0 ? 1 : zoom)),
	    .start_visible = start_marker_visible_,
	    .end_visible = end_marker_visible_,
	    .blink_on = blink_on_,
	};
}

std::array<deluge::ui::widgets::grid::PeakCol, deluge::ui::gfx::grid::kDelugeGrid.main_cols>
AudioClipScene::present_waveform(const deluge::ui::scenes::services::WaveformFrame& wf,
                                 const deluge::ui::views::grid::ClipMarkers& m, bool reversed) const {
	// While the end marker blinks off, the waveform stops one column earlier so the dim red marker column is not
	// painted over.
	int32_t visible_end = m.end_square + 1;
	if (m.end_visible && m.blink_on) {
		visible_end--;
	}
	const int32_t x_end = std::min(kMainCols, visible_end);

	std::array<deluge::ui::widgets::grid::PeakCol, kMainCols> peaks{};
	for (int32_t x = 0; x < x_end; x++) {
		// `renderOneCol`'s own source mapping (`waveform_renderer.cpp`): the reversal picks which SOURCE column an
		// output column reads, which is why `ui::widgets::grid::waveform_full` takes no `reversed` flag (its header
		// says so).
		const int32_t src = reversed ? (kMainCols - 1 - x) : x;
		if (const std::optional<views::waveform::Bar> bar = wf.read.bar(src, wf.centre, wf.span)) {
			peaks[static_cast<size_t>(x)] = {.min24 = bar->bottom, .max24 = bar->top, .valid = true};
		}
	}
	return peaks;
}

RGB AudioClipScene::present_colour() const {
	// `AudioClip::getColour`: the pastel hue its colour offset names.
	const deluge::project::DocClip* record = doc();
	return RGB::fromHuePastel((record != nullptr ? record->colour_offset : 0) * -8 / 3);
}

deluge::ui::views::grid::PadImage AudioClipScene::present_overview() const {
	// `renderClipShortcutsOverview`'s pads: each main pad the colour of the menu shortcut it opens.
	static_assert(deluge::ui::scenes::bind::kOverviewRows == deluge::ui::gfx::grid::kDelugeGrid.rows
	                  && deluge::ui::scenes::bind::kOverviewColumns == deluge::ui::gfx::grid::kDelugeGrid.main_cols,
	              "the overview covers the main pads, pad for pad");
	deluge::ui::scenes::bind::OverviewPads pads{};
	if (deluge::ui::scenes::bind::RootViews* views = root_views(); views != nullptr) {
		views->clip_shortcut_overview(pads);
	}
	deluge::ui::views::grid::PadImage image{};
	for (size_t y = 0; y < pads.size(); ++y) {
		for (size_t x = 0; x < pads[y].size(); ++x) {
			const deluge::ui::scenes::bind::OverviewPad p = pads[y][x];
			image.pads[y][x] = RGB{.r = p.r, .g = p.g, .b = p.b};
		}
	}
	return image;
}

deluge::ui::views::grid::ClipSidebar
AudioClipScene::present_sidebar(bool session_held,
                                const std::array<bool, deluge::ui::gfx::grid::kDelugeGrid.rows>& held) const {
	// `AudioClipView::renderSidebar`: both columns black, and `view.renderMacros(kDisplayWidth, y, -1, ...)` per row in
	// the first while SESSION is held.
	namespace grid = deluge::ui::widgets::grid;
	using deluge::ui::scenes::bind::MacroKind;
	deluge::ui::views::grid::ClipSidebar d{.macros = session_held, .cells = {}};
	if (!session_held) {
		return d;
	}
	const deluge::ui::scenes::bind::SessionMacros* m = macros();
	const bool flash_on = global_controls_ != nullptr && global_controls_->clip_arm_flash_on();
	static_assert(deluge::ui::scenes::bind::SessionMacros::kSlots == kRows, "one macro per row");
	for (uint8_t y = 0; y < d.cells.size(); y++) {
		const deluge::ui::scenes::bind::MacroFact fact =
		    m != nullptr ? m->macro(y) : deluge::ui::scenes::bind::MacroFact{};
		grid::MacroCell& cell = d.cells[y];
		switch (fact.kind) {
		case MacroKind::CLIP_LAUNCH:
			// A clip launch whose clip the song does not hold draws as an unassigned slot, as legacy's `break` does.
			cell.kind = fact.has_clip ? grid::MacroKind::ClipLaunch : grid::MacroKind::None;
			break;
		case MacroKind::OUTPUT_CYCLE:
			cell.kind = grid::MacroKind::OutputCycle;
			break;
		case MacroKind::SECTION:
			cell.kind = grid::MacroKind::Section;
			break;
		case MacroKind::NONE:
			cell.kind = grid::MacroKind::None;
			break;
		}
		cell.clip_active = fact.clip_active;
		cell.clip_armed = fact.clip_armed;
		cell.arm_flash = flash_on;
		cell.held = held[y];
	}
	return d;
}

deluge::ui::views::grid::Playhead AudioClipScene::present_playhead(int32_t tick_col, bool flash) const {
	// `graphicsRoutine`'s `PadLEDs::setTickSquares`: every row gets the same column, and the colour is red (2) while
	// recording linearly, normal (0) otherwise. With the cursor Fast the PIC flashes the square when `setTickSquares`
	// is handed a new one.
	const deluge::ui::widgets::grid::TickColour colour = recording_linearly()
	                                                         ? deluge::ui::widgets::grid::TickColour::Red
	                                                         : deluge::ui::widgets::grid::TickColour::Normal;
	deluge::ui::views::grid::Playhead d{
	    .per_row = {},
	    .paint = flash_cursor() == project::FlashCursor::SLOW && tick_col != deluge::ui::widgets::grid::kNoTickSquare,
	    .muted = kUnreachableMutedColour,
	    .flash = flash,
	    .fast = flash_cursor() == project::FlashCursor::FAST,
	};
	d.per_row.fill(deluge::ui::widgets::grid::TickSquare{.col = static_cast<uint8_t>(tick_col), .colour = colour});
	return d;
}

} // namespace deluge::ui::scenes::audio_clip
