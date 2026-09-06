/// @file
/// @brief `SlicerScene`. See `slicer_scene.h`.

#include "scenes/slicing/slicer_scene.h"

#include "ui/grid/waveform.h"
#include "ui/oled/detail/int_to_string.h"
#include "ui/oled/popup.h"

#include "gfx/grid/palette.h"

#include "model/action/action_logger.h"
#include "model/sample/sample.h"
#include "model/sample/waveform_scan.h"
#include "notify/notice_queue.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::scenes::slicing {

using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// `renderMainPads`' MANUAL arm writes the squashed waveform into `image[yy + 4]` and the slice
/// marks into `image[4]` (`slicer.cpp:119`, `:124`). Legacy spells both as the literal 4; on the
/// shipped 8-row grid that is `kDisplayHeight / 2`, which is also the bound of the squash loop it
/// wrote as an expression (`:118`).
constexpr int32_t kWaveformRowBase = kDisplayHeight / 2;

/// `int32_t x = manualSlicePoints[i].startPos / (sample->lengthInSamples + 0.0) * 16` (`:123`).
/// The 16 is legacy's own literal, not `kDisplayWidth`, and the arithmetic is deliberately in
/// `double` -- `+ 0.0` is what makes it so -- before truncating to `int32_t`.
constexpr int32_t kSliceMarkColumns = 16;

/// `Slicer::renderOLED`'s label (`slicer.cpp:92`). A raw literal in legacy, with no entry in
/// `gui/l10n/strings.h`, so it stays one here -- the same call `MarkerScene` makes for its four
/// page titles.
constexpr std::string_view kNumSlicesLabel = "Num. slices";

/// @brief `strcpy(buffer, prefix); intToString(value, buffer + strlen(buffer));` -- the shape both
///        encoder popups build their text with (`slicer.cpp:224-226`, `:298-300`).
///
/// Not `std::snprintf`: the firmware's own `src/lib/printf.h` `#define`s `snprintf` to
/// `snprintf_` wherever it is visible, and `model/action/action_logger.h` drags it in through
/// `io/debug/log.h`, so `std::snprintf` does not compile in this translation unit. The same
/// substitution is what `menu/menu_dump_main.cpp:178-179` records. `int_to_string(v, 1, ...)` is
/// `intToString(v, buf)` exactly (`ui/oled/detail/int_to_string.h`), and for the one call site
/// legacy spells with `snprintf("%d")` instead (`verticalEncoderAction`, `:242`) the two agree on
/// every value: `%d` and `intToString` differ only in zero-padding, which neither asks for.
///
/// @param prefix The literal before the number.
/// @param value  The number.
/// @param buf    Scratch to build into; the result is truncated to fit.
/// @return A view into @p buf.
std::string_view prefixed_int(std::string_view prefix, int32_t value, std::span<char> buf) {
	const size_t head = std::min(prefix.size(), buf.size());
	std::copy_n(prefix.begin(), head, buf.begin());
	const std::string_view digits =
	    deluge::ui::oled::detail::int_to_string(value, 1, std::span<char>{buf.data() + head, buf.size() - head});
	return std::string_view{buf.data(), head + digits.size()};
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Lifecycle

void SlicerScene::open(Sample* sample) {
	// `SampleBrowser::renderPreviewForTarget` (`gui/ui/browser/sample_browser.cpp:617-619`), onto this
	// scene's own navigator rather than the shared `waveformBasicNavigator` global. `opened()` with
	// no holder is legacy's own call there: no remembered view, so the slicer opens zoomed all the
	// way out.
	nav_.sample = sample;
	if (sample != nullptr) {
		nav_.opened();
	}
}

void SlicerScene::set_mode(Mode mode) {
	// The state half of `buttonAction`'s `X_ENC` arm (`slicer.cpp:285-286`). See the header.
	mode_ = mode;
}

bool SlicerScene::on_enter(const SceneId& id) {
	// `Slicer` has no per-instance argument: one scene, one sample, and the sample arrives through
	// `open` rather than through `SceneId::arg`.
	(void)id;

	// `Slicer::focusRegained` (`slicer.cpp:59-72`), line for line.
	actionLogger.deleteAllLogs(); // `:61`
	num_clips_ = 16;              // `:63`
	num_manual_slices_ = 1;       // `:65`
	current_slice_ = 0;           // `:66`
	mode_ = Mode::Region;         // `:67`
	for (int32_t i = 0; i < kMaxManualSlices; i++) {
		points_[i] = SlicePoint{}; // `:69-70`
	}
	return true;
}

void SlicerScene::on_resume() {
	// `focusRegained` is not only an opening hook: `closeUI` calls it on whatever is left on top
	// after something above closes (`gui/ui/ui.cpp:200`), and `UI::opened()` calls it on the way in
	// (`gui/ui/ui.h:111-113`). So a menu or context menu opened over the slicer and then dismissed
	// wipes the manual slices back to one at the origin, on the device as here. Reproduced, not
	// tidied: it is one call in legacy and `on_resume` is exactly the edge it fires on.
	(void)on_enter(SceneId{kSlicerSceneKind, 0});
}

// ---------------------------------------------------------------------------------------------
// Encoders

Result SlicerScene::horizontal_encoder(int32_t offset, bool shift) {
	Result r;
	r.handled = true; // `:230` returns DEALT_WITH in both modes.
	// DIVERGENCE, labelled like the rest: legacy's guard is the mode alone, and its clamp then
	// dereferences `waveformBasicNavigator.sample` unconditionally (`:220-221`). A scene whose
	// `open()` was handed no sample has nothing to move, so this refuses instead of faulting --
	// the same survivable-spelling choice `MarkerScene::reverse_remap` makes. Unreachable today:
	// `render_grid` draws nothing without a sample either.
	if (mode_ != Mode::Manual || nav_.sample == nullptr) {
		return r;
	}

	// `:208-209`: one detent is 100 samples, or 10 with shift held.
	int32_t new_pos = points_[current_slice_].start_pos;
	new_pos += (shift ? 10 : 100) * offset;

	// `:211-216`. Only an *interior* slice is fenced by its neighbours: the first and the last are
	// free to run into them. Legacy's condition, unchanged.
	if (current_slice_ > 0 && current_slice_ < num_manual_slices_ - 1) {
		if (new_pos <= points_[current_slice_ - 1].start_pos + 1) {
			new_pos = points_[current_slice_ - 1].start_pos + 1;
		}
		if (new_pos >= points_[current_slice_ + 1].start_pos - 1) {
			new_pos = points_[current_slice_ + 1].start_pos - 1;
		}
	}

	// `:218-221`. The negative clamp comes FIRST, which is what makes the comparison on the next
	// line safe: `lengthInSamples` is unsigned, so a negative `new_pos` would convert to a huge
	// value and pass the test. Order preserved deliberately.
	if (new_pos < 0) {
		new_pos = 0;
	}
	if (static_cast<uint64_t>(new_pos) > nav_.sample->lengthInSamples) {
		new_pos = static_cast<int32_t>(nav_.sample->lengthInSamples);
	}
	points_[current_slice_].start_pos = new_pos; // `:222`

	// `:224-227`: `strcpy(buffer, "Start: "); intToString(startPos, buffer + strlen(buffer));`
	// then `display->popupTextTemporary(buffer)`. A computed string, so it travels through the
	// notify seam rather than as an `Action::Kind::Popup`, whose `popup_string` is an l10n id --
	// the same split `MenuScene` makes for its own formatted lines. `popupTextTemporary` is
	// `popupText(text, /*persistent=*/false)` (`hid/display/oled.h:160-162`), i.e. a timed popup,
	// which is what `Notice::Kind::Text` is.
	char buffer[24]; // legacy's own `char buffer[24]` (`:224`)
	deluge::notify::notice_text(prefixed_int("Start: ", points_[current_slice_].start_pos, std::span<char>{buffer}));

	// `:228` `uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF)` -- the PADS only; legacy files no
	// `renderUIsForOled()` here, because the OLED page shows a count this arm does not change.
	// `RepaintAll` is nonetheless what it maps to: `SurfaceRole` names OLED panels only, so
	// `Action::Kind::RepaintAll` is the one action that reaches the grid at all (`shell/scene.h`'s
	// `Action` table). The cost is one extra OLED blit per detent of a knob the user is turning --
	// bounded by input, unlike the timer-armed blits `Scene::timer_repaints()` warns about.
	(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	return r;
}

Result SlicerScene::vertical_encoder(int32_t offset) {
	Result r;
	r.handled = true; // `:245` returns DEALT_WITH in both modes.
	if (mode_ != Mode::Manual) {
		return r;
	}

	// `:236-240`.
	int32_t transpose = points_[current_slice_].transpose + offset;
	if (transpose > 24) {
		transpose = 24;
	}
	if (transpose < -24) {
		transpose = -24;
	}
	points_[current_slice_].transpose = transpose;

	// `:241-243`: `snprintf(buffer, 32, "Transpose: %d", transpose)`, through the notify seam for
	// the reason `horizontal_encoder`'s popup is.
	char buffer[32]; // legacy's own `char buffer[32]` (`:241`)
	deluge::notify::notice_text(prefixed_int("Transpose: ", transpose, std::span<char>{buffer}));

	// No repaint: legacy's `verticalEncoderAction` files none -- the transpose is not on either
	// surface. (`buttonAction`'s `Y_ENC` arm shows the same value as a popup and files none either,
	// `:297-303`.)
	return r;
}

Result SlicerScene::select_encoder(int32_t offset) {
	Result r;
	r.handled = true;

	if (mode_ == Mode::Region) {
		// `:250-256`. The wrap is written against the two values `numClips` lands on after a
		// single detent from either end, so a multi-detent turn steps straight past it -- 255 + 2
		// is 257 and wraps, 256 + 2 is 258 and does not. Legacy's arithmetic, reproduced; the
		// `int16_t` width is part of it, which is why `num_clips_` is one.
		num_clips_ = static_cast<int16_t>(num_clips_ + offset);
		if (num_clips_ == 257) {
			num_clips_ = 2;
		}
		else if (num_clips_ == 1) {
			num_clips_ = 256;
		}
	}
	else {
		// `:258-271`. MANUAL mode only ever *removes* slices from here -- adding them is
		// `padAction`'s (Task 10) -- so a positive turn only re-clamps the cursor.
		if (offset < 0) {
			num_manual_slices_ += offset;
			if (num_manual_slices_ <= 0) {
				num_manual_slices_ = 1;
				points_[0] = SlicePoint{};
			}
		}
		if (current_slice_ >= num_manual_slices_ - 1) {
			current_slice_ = num_manual_slices_ - 1;
		}
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll}); // `:270`
		return r;
	}

	// `:273` `renderUIsForOled()` -- REGION mode redraws the OLED only, since its pads do not
	// depend on the count.
	(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
	return r;
}

Result SlicerScene::handle(const UiEvent& event, const UiContext& ctx) {
	if (event.kind != UiEvent::Kind::Encoder) {
		// `padAction` and the rest of `buttonAction` -- the mode switch, the transpose popup, the
		// slice delete, the commit and BACK -- are Task 10's. Until then a pad or button falls
		// through to whatever is beneath, which is a gap, not a decision: legacy's `Slicer` handles
		// all of them. Nothing links this library into an application yet, so the gap is inert.
		return Result::unhandled();
	}

	switch (event.encoder) {
	case deluge::ui::EncoderRole::Select:
		// Applied here rather than parked for `after_render`, for the reason `MarkerScene::handle`
		// records: `Select` is delivered after every `after_render()` call, so parking it would
		// cost a whole tick per detent.
		return select_encoder(event.delta);
	case deluge::ui::EncoderRole::Horizontal:
		// `Buttons::isShiftButtonPressed()` (`:209`); the shell has already lowered shift, latch
		// included.
		return horizontal_encoder(event.delta, ctx.shift);
	case deluge::ui::EncoderRole::Vertical:
		return vertical_encoder(event.delta);
	default:
		return Result::unhandled();
	}
}

// ---------------------------------------------------------------------------------------------
// Rendering

void SlicerScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	(void)role;
	// `intToString(slicerMode == SLICER_MODE_REGION ? numClips : numManualSlice, buffer)` (`:95`),
	// into legacy's own 12-byte buffer.
	char buffer[12];
	const int32_t value = (mode_ == Mode::Region) ? num_clips_ : num_manual_slices_;
	const std::string_view digits = deluge::ui::oled::detail::int_to_string(value, 1, std::span<char>{buffer});
	deluge::ui::oled::count_window(f, kNumSlicesLabel, digits);
}

void SlicerScene::render_grid(deluge::ui::grid::GridFrame& f) {
	if (nav_.sample == nullptr) {
		return;
	}

	const int32_t main_cols = std::min<int32_t>(f.bounds().w, kDisplayWidth);
	const int32_t rows = std::min<int32_t>(f.bounds().h, kDisplayHeight);
	if (main_cols <= 0 || rows <= 0) {
		return;
	}

	// `renderFullScreen`'s scan (`gui/waveform/waveform_renderer.cpp:51-54`), and its early return:
	// a scan that could not load every cluster leaves the image **untouched** -- it returns before
	// its own clear. So a failed scan draws nothing here either, which is a divergence from
	// `MarkerScene` (which ignores the return) and a match for the routine this scene actually
	// ports.
	if (!deluge::sample::find_peaks_per_col(nav_.sample, nav_.xScroll, static_cast<uint64_t>(nav_.xZoom),
	                                        &nav_.renderData)) {
		return;
	}

	std::array<deluge::ui::grid::PeakCol, kDisplayWidth> peaks{};
	const int32_t centre = nav_.sample->getFoundValueCentrePoint();
	const int32_t span = nav_.sample->getValueSpan();
	for (int32_t x = 0; x < main_cols; x++) {
		if (nav_.renderData.colStatus[x] != COL_STATUS_INVESTIGATED) {
			continue;
		}
		int32_t min24 = 0;
		int32_t max24 = 0;
		deluge::sample::col_bar_positions(x, &nav_.renderData, &min24, &max24, centre, span);
		peaks[static_cast<size_t>(x)] = {.min24 = min24, .max24 = max24, .valid = true};
	}

	f.region(deluge::gfx::grid::Rect{0, 0, main_cols, rows}, [&](deluge::ui::grid::GridFrame& main) {
		// `renderOneCol`'s monochrome arm -- `rgb` is `nullopt` on both of legacy's calls, so
		// `brightness` is 128 (`waveform_renderer.cpp:186`). `waveform_full`'s precondition,
		// honoured.
		deluge::ui::grid::waveform_full(main, peaks, deluge::ui::grid::kWaveformBarBrightnessMono, std::nullopt);

		if (mode_ == Mode::Region) {
			// `:106-110`: the full-screen waveform, and nothing else.
			return;
		}

		// `:113-121`. Legacy renders the waveform full height into a SEPARATE image and then
		// averages row pairs into the top half, so every read is of the untouched full-height
		// render. Done in place that would be wrong -- row 6 reads rows 4 and 5, which rows 0 and 1
		// have already overwritten -- so the full-height render is snapshotted first.
		RGB src[kDisplayHeight][kDisplayWidth];
		for (int32_t y = 0; y < rows; y++) {
			for (int32_t x = 0; x < main_cols; x++) {
				src[y][x] = main.peek(x, y);
			}
		}
		for (int32_t x = 0; x < main_cols; x++) {
			for (int32_t yy = 0; yy < kDisplayHeight / 2; yy++) {
				(void)main.pad(x, yy + kWaveformRowBase, RGB::average(src[yy * 2][x], src[yy * 2 + 1][x]));
			}
		}

		// `:122-129`, the slice marks. The column is `startPos / lengthInSamples * 16` in double
		// arithmetic, so a slice at the very end of the sample lands on column 16 -- the first
		// SIDEBAR column in legacy's image. The region clips it here; see the header for why that
		// is the behaviour-preserving choice rather than a transcription of the write.
		for (int32_t i = 0; i < num_manual_slices_; i++) {
			const int32_t x = static_cast<int32_t>(
			    points_[i].start_pos / (static_cast<double>(nav_.sample->lengthInSamples) + 0.0) * kSliceMarkColumns);
			// `RGB{1, (i == currentSlice) ? 200_u8 : 16_u8, 1}` (`:124-128`). The `_u8` literal lives
			// in `util/misc.h`, which this layer may not include (`scenes-includes-outside`), so
			// the cast is spelled out -- the same reason `MarkerScene` inlined
			// `divide_round_negative`.
			(void)main.pad(x, kWaveformRowBase, RGB(1, static_cast<uint8_t>((i == current_slice_) ? 200 : 16), 1));
		}

		// `:131-149`, the lower screen: 64 slots in four pages of 4x4, laid out left to right by
		// page. `RGB::monochrome(3)` is legacy's initialiser and is overwritten on every path, so
		// it has no counterpart here.
		for (int32_t i = 0; i < kMaxManualSlices; i++) {
			const int32_t xx = (i % 4) + (i / 16) * 4;
			const int32_t yy = (i / 4) % 4;
			const int32_t page = i / 16;

			const auto dim_level = static_cast<uint8_t>((i < num_manual_slices_) ? 2 : 6);
			RGB colour = (page % 2 == 0) ? deluge::gui::colours::green.dim(dim_level)
			                             : deluge::gui::colours::darkblue.dim(dim_level);
			if (i == current_slice_) {
				colour = deluge::gui::colours::green.dim();
			}
			(void)main.pad(xx, yy, colour);
		}
	});
}

SlicerScene& slicer_scene() {
	static SlicerScene scene;
	return scene;
}

} // namespace deluge::scenes::slicing
