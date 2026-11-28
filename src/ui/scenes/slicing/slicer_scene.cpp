/// @file
/// @brief `SlicerScene`. See `slicer_scene.h`.

#include "ui/scenes/slicing/slicer_scene.h"

#include "project/device_settings.h"
#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/browser/browser_kind.h"
#include "ui/scenes/services/waveform_paint.h"

#include "ui/views/grid/waveform.h"
#include "ui/widgets/grid/columns.h"
#include "ui/widgets/grid/waveform.h"
#include "ui/widgets/oled/detail/int_to_string.h"

#include "ui/gfx/grid/palette.h"

#include "base/notify/notice_queue.h"
#include "ui/gfx/grid/pad_grid.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::ui::scenes::slicing {

/// @brief The pad grid's main columns and rows.
constexpr int32_t kMainCols = deluge::ui::gfx::grid::kDelugeGrid.main_cols;
constexpr int32_t kRows = deluge::ui::gfx::grid::kDelugeGrid.rows;

using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// @brief `strcpy(buffer, prefix); intToString(value, buffer + strlen(buffer));` -- the shape both encoder popups build
///        their text with (`Slicer::horizontalEncoderAction` and `Slicer::buttonAction` in
///        `origin/main:src/deluge/gui/ui/slicer.cpp`).
///
/// Not `std::snprintf`: the firmware's own `third_party/printf/printf.h` `#define`s `snprintf` to
/// `snprintf_` wherever it is visible, and `model/action/action_logger.h` drags it in through
/// `io/debug/log.h`, so `std::snprintf` does not compile in this translation unit. The same
/// substitution is what `tests/integration/scenarios/menu_dump.cpp` records. `int_to_string(v, 1, ...)` is
/// `intToString(v, buf)` exactly (`ui/widgets/oled/detail/int_to_string.h`), and for the one call site
/// legacy spells with `snprintf("%d")` instead (`Slicer::verticalEncoderAction`) the two agree on
/// every value: `%d` and `intToString` differ only in zero-padding, which neither asks for.
///
/// @param prefix The literal before the number.
/// @param value  The number.
/// @param buf    Scratch to build into; the result is truncated to fit.
/// @return A view into @p buf.
std::string_view prefixed_int(std::string_view prefix, int32_t value, std::span<char> buf) {
	const size_t head = std::min(prefix.size(), buf.size());
	std::copy_n(prefix.begin(), head, buf.begin());
	const std::string_view digits = deluge::ui::widgets::oled::detail::int_to_string(
	    value, 1, std::span<char>{buf.data() + head, buf.size() - head});
	return std::string_view{buf.data(), head + digits.size()};
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Lifecycle

void SlicerScene::open(std::optional<SliceFile> file, std::optional<views::waveform::ShownWaveform> shown) {
	// `waveformBasicNavigator` as main's `SampleBrowser::previewIfPossible`
	// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) left it: the preview's view. The waveforms port
	// keeps the columns the preview read, by file, so the slicer draws those rather than the overview's coarse peaks.
	if (file && shown) {
		view_ = shown->view;
		file_ = std::move(file);
		retry_.restart();
		return;
	}
	// `waveformBasicNavigator.sample = sample; waveformBasicNavigator.opened();` from
	// `SampleBrowser::previewIfPossible` (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`), onto this
	// scene's own view. Reopened on the file it already shows, the view is kept: legacy's global is not reset between
	// two slicer opens on the same file.
	if (file && file == file_) {
		// A new showing of the same view: columns an earlier showing gave up on are asked for again.
		retry_.restart();
		return;
	}
	file_ = std::move(file);
	if (file_) {
		const std::optional<bind::WaveformInfo> info = slice_info();
		view_ = views::waveform::WaveformView::open(info ? info->length_samples : 0, std::nullopt);
		retry_.restart();
	}
}

void SlicerScene::set_mode(Mode mode) {
	// The state half of `buttonAction`'s `X_ENC` arm (`origin/main:src/deluge/gui/ui/slicer.cpp`). See the header.
	mode_ = mode;
}

bool SlicerScene::on_enter(const SceneId& id) {
	// `Slicer` has no per-instance argument: one scene, one sample, and the sample arrives through
	// `open` rather than through `SceneId::arg`.
	(void)id;

	// `Slicer::focusRegained` (`origin/main:src/deluge/gui/ui/slicer.cpp`), line for line, after dropping a held
	// audition (our legacy's `cancelPreview()` first).
	if (ops_ != nullptr) {
		ops_->cancel_audition();
	}
	// A new visit has no commit out.
	committing_ = false;
	landed_ = false;
	playing_ = false;
	last_ticks_.fill(deluge::ui::widgets::grid::kNoTickSquare);
	reset_state();
	return true;
}

void SlicerScene::reset_state() {
	if (auto* d = bind::port_of(ports_, &bind::Ports::device_actions)) {
		d->delete_all_logs();
	}
	num_clips_ = 16;
	num_manual_slices_ = 1;
	current_slice_ = 0;
	mode_ = Mode::Region;
	for (int32_t i = 0; i < kMaxManualSlices; i++) {
		points_[i] = SlicePoint{};
	}
}

void SlicerScene::on_resume() {
	// `closeUI` calls `focusRegained()` on whatever is left on top once something above it closes
	// (`gui/ui/ui.cpp`), and legacy's `Slicer` gets nothing else on that edge -- so this is
	// `Slicer::focusRegained` (`origin/main:src/deluge/gui/ui/slicer.cpp`) again, exactly as `on_enter` is.
	//
	// A popup this scene raises is a shell scene, where legacy's is a display overlay that never reaches `closeUI`;
	// `SceneStack` fires `on_resume()` only when `input_top()` changes hands, and an `input_transparent()` popup never
	// holds it, so a popup coming and going leaves the slice table alone. A real scene closing above the slicer gets
	// legacy's reset. Both are asserted in `tests/integration/ui/scenes/slicer_scene_spec.cpp`.
	if (ops_ != nullptr) {
		ops_->cancel_audition();
	}
	reset_state();
}

// ---------------------------------------------------------------------------------------------
// Encoders

Result SlicerScene::horizontal_encoder(int32_t offset, bool shift) {
	Result r;
	// `Slicer::horizontalEncoderAction` returns DEALT_WITH in both modes.
	r.handled = true;
	// DIVERGENCE, labelled like the rest: legacy's guard is the mode alone, and its clamp then
	// dereferences `waveformBasicNavigator.sample` unconditionally. A scene whose
	// `open()` was handed no sample has nothing to move, so this refuses instead of faulting --
	// the same survivable-spelling choice `MarkerScene::reverse_remap` makes. Unreachable today:
	// `render_grid` draws nothing without a sample either.
	if (mode_ != Mode::Manual || !file_) {
		return r;
	}

	// One detent is 100 samples, or 10 with shift held.
	int32_t new_pos = points_[current_slice_].start_pos;
	new_pos += (shift ? 10 : 100) * offset;

	// Only an *interior* slice is fenced by its neighbours: the first and the last are
	// free to run into them. Legacy's condition, unchanged.
	if (current_slice_ > 0 && current_slice_ < num_manual_slices_ - 1) {
		if (new_pos <= points_[current_slice_ - 1].start_pos + 1) {
			new_pos = points_[current_slice_ - 1].start_pos + 1;
		}
		if (new_pos >= points_[current_slice_ + 1].start_pos - 1) {
			new_pos = points_[current_slice_ + 1].start_pos - 1;
		}
	}

	// The negative clamp comes FIRST, which is what makes the comparison on the next
	// line safe: the length is unsigned, so a negative `new_pos` would convert to a huge
	// value and pass the test. Order preserved deliberately.
	if (new_pos < 0) {
		new_pos = 0;
	}
	if (static_cast<uint64_t>(new_pos) > view_.length()) {
		new_pos = static_cast<int32_t>(view_.length());
	}
	points_[current_slice_].start_pos = new_pos;

	// `strcpy(buffer, "Start: "); intToString(startPos, buffer + strlen(buffer));`
	// then `display->popupTextTemporary(buffer)`. A computed string, so it travels through the
	// notify seam rather than as an `Action::Kind::Popup`, whose `popup_string` is an l10n id --
	// the same split `MenuScene` makes for its own formatted lines. `popupTextTemporary` is
	// `popupText(text, /*persistent=*/false)` (`hid/display/oled.h`), i.e. a timed popup,
	// which is what `Notice::Kind::Text` is.
	char buffer[24]; // legacy's own `char buffer[24]`
	deluge::base::notify::notice_text(
	    prefixed_int("Start: ", points_[current_slice_].start_pos, std::span<char>{buffer}));

	// `uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF)` -- the PADS only; legacy files no
	// `renderUIsForOled()` here, because the OLED page shows a count this arm does not change.
	// `RepaintAll` is nonetheless what it maps to: `SurfaceRole` names OLED panels only, so
	// `Action::Kind::RepaintAll` is the one action that reaches the grid at all (`ui/shell/scene.h`'s
	// `Action` table). The cost is one extra OLED blit per detent of a knob the user is turning --
	// bounded by input, unlike the timer-armed blits `Scene::timer_repaints()` warns about.
	(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	return r;
}

Result SlicerScene::vertical_encoder(int32_t offset) {
	Result r;
	// `Slicer::verticalEncoderAction` returns DEALT_WITH in both modes.
	r.handled = true;
	if (mode_ != Mode::Manual) {
		return r;
	}

	// The clamp to +-24 semitones.
	int32_t transpose = points_[current_slice_].transpose + offset;
	if (transpose > 24) {
		transpose = 24;
	}
	if (transpose < -24) {
		transpose = -24;
	}
	points_[current_slice_].transpose = transpose;

	// `snprintf(buffer, 32, "Transpose: %d", transpose)`, through the notify seam for
	// the reason `horizontal_encoder`'s popup is.
	char buffer[32]; // legacy's own `char buffer[32]`
	deluge::base::notify::notice_text(prefixed_int("Transpose: ", transpose, std::span<char>{buffer}));

	// No repaint: legacy's `verticalEncoderAction` files none -- the transpose is not on either
	// surface. (`Slicer::buttonAction`'s `Y_ENC` arm shows the same value as a popup and files none either.)
	return r;
}

Result SlicerScene::select_encoder(int32_t offset) {
	Result r;
	r.handled = true;

	if (mode_ == Mode::Region) {
		// The wrap is written against the two values `numClips` lands on after a
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
		// MANUAL mode only ever *removes* slices from here -- adding them is
		// `padAction`'s -- so a positive turn only re-clamps the cursor.
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
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	// `renderUIsForOled()` -- REGION mode redraws the OLED only, since its pads do not
	// depend on the count.
	(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
	return r;
}

// ---------------------------------------------------------------------------------------------
// The audition preview -- `Slicer::preview`

void SlicerScene::preview(const PreviewRequest& req) {
	// The host holds the audition's load and sounds it when it lands (`Slicer::preview`,
	// `origin/main:src/deluge/gui/ui/slicer.cpp`).
	if (ops_ != nullptr) {
		ops_->audition(req);
	}
}

// ---------------------------------------------------------------------------------------------
// The commit's end

void SlicerScene::commit_done(bool success) {
	if (!committing_) {
		return; // nothing out: a stale answer
	}
	committing_ = false;
	landed_ = success;
}

Result SlicerScene::after_render() {
	Result r;
	if (std::exchange(landed_, false)) {
		// `doSlice`'s `exitAndNeverDeleteDrum` closes the slicer with the menu and browser beneath it; the host
		// closes those two.
		if (ops_ != nullptr) {
			ops_->closed();
		}
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kSlicerSceneKind});
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}
	return r;
}

std::optional<uint32_t> SlicerScene::next_timer() const {
	if (landed_) {
		return 0;
	}
	// `graphicsRoutine` runs every pass: while a voice plays, and for the frame that clears the playhead it leaves.
	if (playing_ || (ops_ != nullptr && ops_->play_position().has_value())) {
		return kPlayheadMs;
	}
	return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// Pads -- `Slicer::padAction`

bool SlicerScene::add_slice_at(int32_t sample_pos) {
	// `Slicer::padAction`'s do-slice arm, with the play position handed in. Legacy compares a signed `samplePos`
	// against an unsigned `lengthInSamples`, which promotes the comparison to unsigned -- so a negative position would
	// pass the test. The port cannot reach that: `SliceOps::play_position` returns `nullopt` where legacy has no voice,
	// and a real play position is never negative. The comparison is spelled with the cast that makes the promotion
	// visible rather than silent.
	if (!file_) {
		return false;
	}
	if (static_cast<uint64_t>(static_cast<uint32_t>(sample_pos)) >= view_.length()
	    || num_manual_slices_ >= kMaxManualSlices) {
		return false;
	}

	points_[num_manual_slices_] = SlicePoint{.start_pos = sample_pos, .transpose = 0};
	num_manual_slices_++;
	deluge::base::notify::notice_kind(deluge::base::notify::Notice::Kind::CancelPopup);

	// Legacy's own bubble sort over the placed slices, kept as a bubble sort: it is
	// `numManualSlice` <= 64 elements on a pad press, and swapping it for something else would be a
	// change to what the table looks like after an equal-key insertion.
	for (int32_t i = 0; i < num_manual_slices_ - 1; i++) {
		for (int32_t j = num_manual_slices_ - 1; j > i; j--) {
			if (points_[j].start_pos < points_[j - 1].start_pos) {
				const SlicePoint tmp = points_[j];
				points_[j] = points_[j - 1];
				points_[j - 1] = tmp;
			}
		}
	}
	return true;
}

const bind::Waveforms* SlicerScene::waveforms() const {
	return bind::port_of(ports_, &bind::Ports::waveforms);
}

std::optional<bind::WaveformInfo> SlicerScene::slice_info() const {
	const bind::Waveforms* port = waveforms();
	if (port == nullptr) {
		return std::nullopt;
	}
	return port->info({.kind = bind::WaveformAddress::Kind::SLICE});
}

Result SlicerScene::pad_action(int32_t x, int32_t y, int32_t velocity) {
	Result r;
	const bool on = velocity != 0;
	// `Slicer::padAction`'s press and release arms are the same window -- a main-grid pad in the TOP half, in
	// MANUAL mode. `kDisplayHeight / 2` is legacy's own expression for the slot grid's four rows.
	const bool in_window = x < kMainCols && y < kRows / 2 && mode_ == Mode::Manual;

	if (on && in_window) {
		// The inverse of the slot layout `render_grid` draws with -- index `i` goes to
		// `((i % 4) + (i / 16) * 4, (i / 4) % 4)` there, and this brings it back.
		const int32_t index = (x % 4 + (x / 4) * 16) + ((y % 4) * 4);

		// Play slice.
		if (index < num_manual_slices_) {
			const bool close_popup = current_slice_ != index;
			current_slice_ = index;
			if (file_) {
				// The two arms differ only in where the slice ends: the next slice's
				// start, or the end of the sample for the last one. A third case cannot arise --
				// `index < num_manual_slices_` makes `index + 1 > num_manual_slices_` impossible --
				// so legacy's `else if` is an `else` in effect, and is kept as legacy spells it.
				const int64_t end = (index + 1 < num_manual_slices_)
				                        ? static_cast<int64_t>(points_[index + 1].start_pos)
				                        : static_cast<int64_t>(view_.length());
				preview(PreviewRequest{.start_point = points_[index].start_pos,
				                       .end_point = end,
				                       .transpose = points_[index].transpose,
				                       .on = velocity});
			}
			if (close_popup) {
				// `display->cancelPopup()`, through the notify seam -- the popup service
				// owns the panel, so "take mine down" is a notice, not a frame write.
				deluge::base::notify::notice_kind(deluge::base::notify::Notice::Kind::CancelPopup);
			}
		}
		// Do slice.
		else {
			// The voice walk is the host's -- see `SliceOps::play_position`.
			// The `if (voiceSample != nullptr)` after it is exactly "the walk found one", which is what
			// an engaged optional means here.
			//
			// NOT a defect, but worth writing down because it reads like one: legacy declares
			// `MultisampleRange* range;` uninitialised, captures it BY REFERENCE into the filter predicate, and only
			// assigns it -- one statement AFTER the view is built. It is safe, and only because `std::views::filter` is
			// lazy: the predicate is first evaluated by `valid_voices_view.empty()`, which is after the assignment.
			// `Slicer::graphicsRoutine` does the same walk with the assignment FIRST, so the two orderings differ for
			// no reason and the padAction one is a trap for anyone who makes the view eager or the capture by value.
			if (ops_ != nullptr) {
				if (const std::optional<int32_t> pos = ops_->play_position(); pos.has_value()) {
					(void)add_slice_at(*pos);
				}
			}
		}

		// The tail of the press arm, inside the outer `if` and therefore fired by BOTH branches above --
		// including a "do slice" that placed nothing.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	if (!on && in_window) {
		// A release in the window silences the audition and repaints nothing.
		preview(PreviewRequest{.start_point = 0, .end_point = 0, .transpose = 0, .on = 0});
		r.handled = true;
		return r;
	}

	if (mode_ == Mode::Manual) {
		// In MANUAL every pad is DEALT_WITH, in or out of the window -- the sidebar and
		// the bottom half included.
		r.handled = true;
		return r;
	}

	// Hands a REGION pad to `SampleBrowser::padAction`. Its mute-column arm (`possiblyExit`): a press stops the
	// preview and exits the browser, and the slicer over it with it. Its audition column is the view beneath's
	// (`grid_pad_claim`). Its keyboard would move the file under a slicer slicing another: swallowed instead
	// (DIFFERENCES D57).
	r.handled = true;
	// `possiblyExit`'s `!currentUIMode`. Under `main`'s slicer only two UI modes can be up: an audition held on the
	// view beneath (`UI_MODE_AUDITIONING`) and a slice commit (`UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES`, this scene's
	// `committing_`, whose pads `handle` swallows before they reach here). The browser's own modes (a held Select or
	// X_ENC, a held backspace, a scroll) need inputs the slicer takes for itself or swallows.
	const bind::UiFacts* ui = bind::port_of(ports_, &bind::Ports::ui);
	const bool auditioning = ui != nullptr && ui->auditioning();
	if (x == kMainCols && on && !auditioning) {
		if (ops_ != nullptr) {
			ops_->stop_previewing();
			ops_->closed();
		}
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kSlicerSceneKind});
		(void)r.actions.push_back(
		    Action{.kind = Action::Kind::Remove, .pop_to = deluge::ui::scenes::browser::kBrowserSceneKind});
	}
	return r;
}

// ---------------------------------------------------------------------------------------------
// Buttons -- `Slicer::buttonAction` (`origin/main:src/deluge/gui/ui/slicer.cpp`)

void SlicerScene::delete_current_slice() {
	// The shift-down runs over the WHOLE table, not just the placed slices, so the tail
	// entry is always the one that gets cleared.
	const int32_t target = current_slice_;
	for (int32_t i = 0; i < kMaxManualSlices - 1; i++) {
		points_[i] = points_[(i >= target) ? i + 1 : i];
	}
	points_[kMaxManualSlices - 1] = SlicePoint{};

	num_manual_slices_--;
	if (num_manual_slices_ <= 0) {
		// The same floor `selectEncoderAction`'s MANUAL arm applies.
		num_manual_slices_ = 1;
		points_[0] = SlicePoint{};
	}
	if (current_slice_ >= num_manual_slices_ - 1) {
		current_slice_ = num_manual_slices_ - 1;
	}
}

Result SlicerScene::button_action(ButtonId b, const UiContext& ctx) {
	Result r;

	// `Slicer::buttonAction`'s mode switch.
	if (b == ButtonId::XEnc) {
		mode_ = (mode_ == Mode::Region) ? Mode::Manual : Mode::Region;
		if (ops_ != nullptr) {
			ops_->cancel_audition();
			if (mode_ == Mode::Manual) {
				ops_->stop_previewing();
			}
			// Voices are killed on both mode transitions.
			ops_->kill_voices();
		}
		// `renderUIsForOled()` and `uiNeedsRendering(this, ...)` together are
		// `RepaintAll`.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	// The transpose popup. Note the extra condition legacy's own encoder arm does not
	// have: `currentSlice < numManualSlice`.
	if (b == ButtonId::YEnc && mode_ == Mode::Manual && current_slice_ < num_manual_slices_) {
		char buffer[24]; // legacy's own `char buffer[24]`
		deluge::base::notify::notice_text(
		    prefixed_int("Transpose: ", points_[current_slice_].transpose, std::span<char>{buffer}));
		r.handled = true;
		return r;
	}

	// The delete. The pad the cursor sits on has to be HELD -- legacy asks
	// `matrixDriver.isPadPressed(xx, yy)` with the slot-grid coordinates `render_grid` draws at.
	// **The braces matter here:** the whole arm is inside `if (matrixDriver.isPadPressed(...) &&
	// currentSlice < numManualSlice)`, and when that test fails the arm does NOT return -- it falls
	// past the SELECT_ENC/BACK chain to the final `else` of `Slicer::buttonAction` and answers NOT_DEALT_WITH.
	if (b == ButtonId::Save && mode_ == Mode::Manual) {
		const int32_t xx = (current_slice_ % 4) + (current_slice_ / 16) * 4;
		const int32_t yy = (current_slice_ / 4) % 4;
		if (ctx.held_pads.test(static_cast<uint8_t>(xx), static_cast<uint8_t>(yy))
		    && current_slice_ < num_manual_slices_) {
			delete_current_slice();
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
			return r;
		}
		return Result::unhandled();
	}

	if (b == ButtonId::SelectEnc) {
		// Snapshot the gesture: plain values only, so nothing pointing into `points_`, `num_manual_slices_` or
		// `num_clips_` survives past this point, and an encoder turn while the slice's load is out changes nothing
		// it slices. Main's MANUAL arm also writes `numClips = numManualSlice`; this leaves the REGION count alone.
		if (ops_ != nullptr) {
			ops_->cancel_audition();
		}
		pending_commit_.is_manual = mode_ == Mode::Manual;
		pending_commit_.slice_count = pending_commit_.is_manual ? num_manual_slices_ : num_clips_;
		if (pending_commit_.is_manual) {
			std::copy(std::begin(points_), std::end(points_), std::begin(pending_commit_.points));
			if (ops_ != nullptr) {
				ops_->kill_voices();
			}
		}
		// The gate, until the host ends the slice (`commit_done`): every button and pad is refused while it is out.
		committing_ = ops_ != nullptr && ops_->begin_commit(pending_commit_);
		r.handled = true;
		return r;
	}

	if (b == ButtonId::Back) {
		// After dropping a held audition.
		if (ops_ != nullptr) {
			ops_->cancel_audition();
		}
		if (mode_ == Mode::Manual && ops_ != nullptr) {
			// Renders the full-height waveform into `PadLEDs::image` on the way out --
			// legacy's parting frame for the transition it is about to run. It has no counterpart
			// here and needs none: the shell recomposes the grid bottom-up on the next tick from
			// whatever scenes are then on the stack, so there is no image to leave behind.
			// (`Slicer::buttonAction`'s own `RGB myImage[...]` is 432 bytes of dead stack -- declared, never read,
			// never written, since the render goes into `PadLEDs::image` instead. The REGION arm of
			// `renderMainPads` has the identical dead array. Recorded in
			// the first entry of `src/ui/scenes/README.md`'s "Defects this port found in shipping
			// firmware"; not fixed, and nothing to port.)
			ops_->kill_voices();
			ops_->restore_full_range();
		}
		// `close()`. `Remove` and not `Pop`, for the reason `ui/shell/scene.h`'s note on
		// `Action::Kind::Pop` gives: a `Popup` this scene itself raised can legally be above it by
		// the time the router applies the action, and a blind `Pop` would take that instead.
		if (ops_ != nullptr) {
			ops_->closed();
		}
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kSlicerSceneKind});
		return r;
	}

	// Everything else falls through.
	return Result::unhandled();
}

Result SlicerScene::handle(const UiEvent& event, const UiContext& ctx) {
	switch (event.kind) {
	case UiEvent::Kind::Pad:
		// Legacy's third parameter is the raw velocity, which it names `on` and passes straight
		// through to `preview` (`Slicer::padAction`). The shell keeps `on == (velocity != 0)` invariantly
		// (`InputLowering::on_pad`, `ui/shell/input_lowering.cpp`), so the velocity alone is
		// exactly what legacy is handed. Refused while a commit is out (our legacy's `slicePhase_` gate).
		if (committing_) {
			Result swallowed;
			swallowed.handled = true;
			return swallowed;
		}
		return pad_action(event.x, event.y, event.velocity);

	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		// The top-of-function guard of `Slicer::buttonAction`. `!on` is the release half; `currentUIMode !=
		// UI_MODE_NONE` is the commit gate, which is this scene's `committing_` -- legacy's other
		// UI modes are global state the shell does not have, and the ones a slicer could see
		// (a held encoder button, a held pad) are questions `UiContext` answers instead.
		if (!event.on || committing_) {
			return Result::unhandled();
		}
		return button_action(event.button, ctx);

	case UiEvent::Kind::Encoder:
		break;

	// The MIDI kinds (`ui/shell/scene.h`) are declined here, as every non-timeline scene declines them: claiming one
	// means answering `learnable_at(pad)`, which is the timeline scenes' work. Declining lets `Router::run_chain` carry
	// the message on to `services::MidiLearn`, which is where legacy's `view.` fallback puts it.
	case UiEvent::Kind::MidiNoteOn:
	case UiEvent::Kind::MidiCc:
	case UiEvent::Kind::MidiProgramChange:
	case UiEvent::Kind::MidiPitchBend:
	case UiEvent::Kind::AllReleased:
		return Result::unhandled();
	}

	switch (event.encoder) {
	case deluge::ui::widgets::core::EncoderRole::Select:
		// Applied here, before the tick's render, so the frame that follows already shows the move.
		return select_encoder(event.delta);
	case deluge::ui::widgets::core::EncoderRole::Horizontal:
		// `Buttons::isShiftButtonPressed()`; the shell has already lowered shift, latch
		// included.
		return horizontal_encoder(event.delta, ctx.shift);
	case deluge::ui::widgets::core::EncoderRole::Vertical:
		return vertical_encoder(event.delta);
	default:
		return Result::unhandled();
	}
}

// ---------------------------------------------------------------------------------------------
// Rendering

void SlicerScene::render_oled(SurfaceRole role, deluge::ui::widgets::oled::OledFrame& f) {
	(void)role;
	char buffer[12];
	deluge::ui::views::oled::slicer_count(f, present_count(std::span<char, 12>{buffer}));
}

void SlicerScene::render_grid(deluge::ui::widgets::grid::GridFrame& f) {
	render_pads(f);
	playing_ = false;
	if (!file_ || ops_ == nullptr) {
		return;
	}
	const std::optional<int32_t> pos = ops_->play_position();
	playing_ = pos.has_value();
	deluge::ui::views::grid::playhead(f, present_playhead(pos), last_ticks_);
}

void SlicerScene::render_pads(deluge::ui::widgets::grid::GridFrame& f) {
	const bind::Waveforms* port = waveforms();
	if (!file_ || port == nullptr) {
		return;
	}

	// **The region is legacy's fixed 16x8, not the frame's own extent, and there is no clamp.**
	// Every coordinate this scene draws at is a hard-coded constant in `renderMainPads`: the
	// squashed waveform goes to rows 4..7 (`origin/main:src/deluge/gui/ui/slicer.cpp`), the slice marks to row 4
	// and the 64-slot grid is four pages of 4x4 laid out by `(i % 4) + (i / 16) * 4`
	// and `(i / 4) % 4`, which has no meaning at any other size. `drawColBar` centres
	// its bars on `kDisplayHeight >> 1` too (`WaveformRenderer::drawColBar`,
	// `gui/waveform/waveform_renderer.cpp`), not on whatever height it was handed -- so
	// asking for exactly `kDisplayHeight` is what makes `ui::widgets::grid::waveform_full`, which centres on
	// `f.bounds().h >> 1`, agree with it.
	//
	// A frame that cannot grant all of that is handled by `GridFrame` itself: `pad()` outside
	// `visible()` paints nothing and `peek()` returns black, which is exactly what a clipped write
	// would have left behind, so there is no clamp of this function's own.

	// `renderFullScreen`'s scan (`origin/main:src/deluge/gui/waveform/waveform_renderer.cpp`), and its early return:
	// a scan that could not load every cluster leaves the image **untouched** -- it returns before its own clear.
	const bind::WaveformAddress slice{.kind = bind::WaveformAddress::Kind::SLICE};
	const std::optional<services::WaveformFrame> wf = services::read_waveform(*port, slice, view_.window());
	if (!wf) {
		return;
	}
	// A read with a column the card could not give draws nothing this frame, and the frame after asks again.
	if (retry_.after(wf->read, view_.window(), f.now_ms()) == services::PeakRetry::Next::ASK_AGAIN) {
		f.grid_gate().request_repaint_after(services::kPeakRetryPollMs, f.now_ms());
	}
	if (wf->read.pending()) {
		return;
	}

	f.region(deluge::ui::gfx::grid::Rect{0, 0, kMainCols, kRows}, [&](deluge::ui::widgets::grid::GridFrame& main) {
		deluge::ui::views::grid::waveform(main, present_waveform(*wf),
		                                  deluge::ui::widgets::grid::kWaveformBarBrightnessMono, std::nullopt);
		deluge::ui::views::grid::slice_marks(main, present_marks());
	});
}

SlicerScene& slicer_scene() {
	static SlicerScene scene;
	return scene;
}

} // namespace deluge::ui::scenes::slicing
