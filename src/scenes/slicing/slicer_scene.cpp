/// @file
/// @brief `SlicerScene`. See `slicer_scene.h`.

#include "scenes/slicing/slicer_scene.h"

#include "scenes/menu/bind_host.h"
#include "scenes/services/waveform_paint.h"

#include "ui/grid/columns.h"
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
#include <utility>

namespace deluge::scenes::slicing {

using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
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

void SlicerScene::open(Sample* sample, const deluge::sample::WaveformNav* shown) {
	// `waveformBasicNavigator` as `SampleBrowser::showPreview` left it: the preview's view, and the columns it already
	// read from the audio. A fresh scan would read the overview cache's coarse peaks instead.
	if (sample != nullptr && shown != nullptr) {
		nav_ = *shown;
		nav_.sample = sample;
		nav_path_ = sample->filePath;
		return;
	}
	// `SampleBrowser::showPreview`'s `waveformBasicNavigator.sample = sample; waveformBasicNavigator.opened();` onto
	// this scene's own navigator. Reopened on the sample it already shows, the navigator is kept, render cache and
	// all: legacy's global is not reset between two slicer opens on the same file.
	if (sample != nullptr && sample == nav_.sample && sample->filePath == nav_path_) {
		return;
	}
	nav_.sample = sample;
	nav_path_ = sample != nullptr ? sample->filePath : std::string{};
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

	// `Slicer::focusRegained` (`slicer.cpp:59-72`), line for line, after dropping a held audition (our legacy's
	// `cancelPreview()` first).
	if (ops_ != nullptr) {
		ops_->cancel_audition();
	}
	// A new visit has no commit out.
	committing_ = false;
	landed_ = false;
	playing_ = false;
	last_ticks_.fill(deluge::ui::grid::kNoTickSquare);
	reset_state();
	return true;
}

void SlicerScene::reset_state() {
	actionLogger.deleteAllLogs(); // `:61`
	num_clips_ = 16;              // `:63`
	num_manual_slices_ = 1;       // `:65`
	current_slice_ = 0;           // `:66`
	mode_ = Mode::Region;         // `:67`
	for (int32_t i = 0; i < kMaxManualSlices; i++) {
		points_[i] = SlicePoint{}; // `:69-70`
	}
}

void SlicerScene::on_resume() {
	// `closeUI` calls `focusRegained()` on whatever is left on top once something above it closes
	// (`gui/ui/ui.cpp:223-229`), and legacy's `Slicer` gets nothing else on that edge -- so this is
	// `Slicer::focusRegained` (`gui/ui/slicer.cpp:59-72`) again, exactly as `on_enter` is.
	//
	// A popup this scene raises is a shell scene, where legacy's is a display overlay that never reaches `closeUI`;
	// `SceneStack` fires `on_resume()` only when `input_top()` changes hands, and an `input_transparent()` popup never
	// holds it, so a popup coming and going leaves the slice table alone. A real scene closing above the slicer gets
	// legacy's reset. Both are asserted in `sim/scenes_spec/slicer_scene_spec.cpp`.
	if (ops_ != nullptr) {
		ops_->cancel_audition();
	}
	reset_state();
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
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll}); // `:270`
		return r;
	}

	// `:273` `renderUIsForOled()` -- REGION mode redraws the OLED only, since its pads do not
	// depend on the count.
	(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
	return r;
}

// ---------------------------------------------------------------------------------------------
// The audition preview -- `Slicer::preview`

void SlicerScene::preview(const PreviewRequest& req) {
	// The host holds the audition's load and sounds it when it lands (`Slicer::preview`, `slicer.cpp:438-485`).
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
	// `:535-551`, with the play position handed in. Legacy compares a signed `samplePos` against an
	// unsigned `lengthInSamples`, which promotes the comparison to unsigned -- so a negative
	// position would pass the test. The port cannot reach that: `SliceOps::play_position` returns
	// `nullopt` where legacy has no voice, and a real play position is never negative. The
	// comparison is spelled with the cast that makes the promotion visible rather than silent.
	if (nav_.sample == nullptr) {
		return false;
	}
	if (static_cast<uint64_t>(static_cast<uint32_t>(sample_pos)) >= nav_.sample->lengthInSamples
	    || num_manual_slices_ >= kMaxManualSlices) {
		return false;
	}

	points_[num_manual_slices_] = SlicePoint{.start_pos = sample_pos, .transpose = 0}; // `:536-537`
	num_manual_slices_++;                                                              // `:539`
	deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelPopup);            // `:540`

	// `:542-551`, legacy's own bubble sort over the placed slices, kept as a bubble sort: it is
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

Result SlicerScene::pad_action(int32_t x, int32_t y, int32_t velocity) {
	Result r;
	const bool on = velocity != 0;
	// `:486` and `:559`: both arms are the same window -- a main-grid pad in the TOP half, in
	// MANUAL mode. `kDisplayHeight / 2` is legacy's own expression for the slot grid's four rows.
	const bool in_window = x < kDisplayWidth && y < kDisplayHeight / 2 && mode_ == Mode::Manual;

	if (on && in_window) {
		// `:488`. The inverse of the slot layout `render_grid` draws with -- index `i` goes to
		// `((i % 4) + (i / 16) * 4, (i / 4) % 4)` there, and this brings it back.
		const int32_t index = (x % 4 + (x / 4) * 16) + ((y % 4) * 4);

		if (index < num_manual_slices_) {                     // `:490` play slice
			const bool close_popup = current_slice_ != index; // `:491`
			current_slice_ = index;                           // `:492`
			if (nav_.sample != nullptr) {
				// `:493-500`. The two arms differ only in where the slice ends: the next slice's
				// start, or the end of the sample for the last one. A third case cannot arise --
				// `index < num_manual_slices_` makes `index + 1 > num_manual_slices_` impossible --
				// so legacy's `else if` is an `else` in effect, and is kept as legacy spells it.
				const int64_t end = (index + 1 < num_manual_slices_)
				                        ? static_cast<int64_t>(points_[index + 1].start_pos)
				                        : static_cast<int64_t>(nav_.sample->lengthInSamples);
				preview(PreviewRequest{.start_point = points_[index].start_pos,
				                       .end_point = end,
				                       .transpose = points_[index].transpose,
				                       .on = velocity});
			}
			if (close_popup) {
				// `:502-504` `display->cancelPopup()`, through the notify seam -- the popup service
				// owns the panel, so "take mine down" is a notice, not a frame write.
				deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelPopup);
			}
		}
		else { // `:506` do slice
			// `:508-534` is the voice walk, which the host runs -- see `SliceOps::play_position`.
			// `:533`'s `if (voiceSample != nullptr)` is exactly "the walk found one", which is what
			// an engaged optional means here.
			//
			// NOT a defect, but worth writing down because it reads like one: legacy declares
			// `MultisampleRange* range;` uninitialised at `:510`, captures it BY REFERENCE into the
			// filter predicate at `:515-518`, and only assigns it at `:520` -- one line AFTER the
			// view is built. It is safe, and only because `std::views::filter` is lazy: the
			// predicate is first evaluated by `valid_voices_view.empty()` at `:522`, which is after
			// the assignment. `graphicsRoutine` does the same walk with the assignment FIRST
			// (`:167` before `:169`), so the two orderings differ for no reason and the padAction
			// one is a trap for anyone who makes the view eager or the capture by value.
			if (ops_ != nullptr) {
				if (const std::optional<int32_t> pos = ops_->play_position(); pos.has_value()) {
					(void)add_slice_at(*pos);
				}
			}
		}

		// `:556-557`, inside the outer `if` and therefore fired by BOTH branches above -- including
		// a "do slice" that placed nothing.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	if (!on && in_window) {
		// `:559-561`: a release in the window silences the audition and repaints nothing.
		preview(PreviewRequest{.start_point = 0, .end_point = 0, .transpose = 0, .on = 0});
		r.handled = true;
		return r;
	}

	if (mode_ == Mode::Manual) {
		// `:563-565`: in MANUAL every pad is DEALT_WITH, in or out of the window -- the sidebar and
		// the bottom half included.
		r.handled = true;
		return r;
	}

	// `:567` hands a REGION pad to the sample browser beneath, whose keyboard would move the file under a slicer
	// slicing another. Swallowed instead (DIFFERENCES records it).
	r.handled = true;
	return r;
}

// ---------------------------------------------------------------------------------------------
// Buttons -- `Slicer::buttonAction` (`slicer.cpp:276-398`)

void SlicerScene::delete_current_slice() {
	// `:310-326`. The shift-down runs over the WHOLE table, not just the placed slices, so the tail
	// entry is always the one that gets cleared.
	const int32_t target = current_slice_;
	for (int32_t i = 0; i < kMaxManualSlices - 1; i++) {
		points_[i] = points_[(i >= target) ? i + 1 : i];
	}
	points_[kMaxManualSlices - 1] = SlicePoint{}; // `:315-316`

	num_manual_slices_--; // `:318`
	if (num_manual_slices_ <= 0) {
		// `:319-323`, the same floor `selectEncoderAction`'s MANUAL arm applies (`:261-265`).
		num_manual_slices_ = 1;
		points_[0] = SlicePoint{};
	}
	if (current_slice_ >= num_manual_slices_ - 1) {
		current_slice_ = num_manual_slices_ - 1; // `:324-326`
	}
}

Result SlicerScene::button_action(ButtonId b, const UiContext& ctx) {
	Result r;

	// `:284-294`, the mode switch.
	if (b == ButtonId::XEnc) {
		mode_ = (mode_ == Mode::Region) ? Mode::Manual : Mode::Region; // `:285-286`
		if (ops_ != nullptr) {
			ops_->cancel_audition();
			if (mode_ == Mode::Manual) {
				ops_->stop_previewing(); // `:287-288`
			}
			ops_->kill_voices(); // `:291`, on both mode transitions
		}
		// `:289` `renderUIsForOled()` and `:292` `uiNeedsRendering(this, ...)` together are
		// `RepaintAll`.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	// `:297-303`, the transpose popup. Note the extra condition legacy's own encoder arm does not
	// have: `currentSlice < numManualSlice`.
	if (b == ButtonId::YEnc && mode_ == Mode::Manual && current_slice_ < num_manual_slices_) {
		char buffer[24]; // legacy's own `char buffer[24]` (`:298`)
		deluge::notify::notice_text(
		    prefixed_int("Transpose: ", points_[current_slice_].transpose, std::span<char>{buffer}));
		r.handled = true;
		return r;
	}

	// `:306-332`, the delete. The pad the cursor sits on has to be HELD -- legacy asks
	// `matrixDriver.isPadPressed(xx, yy)` with the slot-grid coordinates `render_grid` draws at.
	// **The braces matter here:** the whole arm is inside `if (matrixDriver.isPadPressed(...) &&
	// currentSlice < numManualSlice)`, and when that test fails the arm does NOT return -- it falls
	// past the SELECT_ENC/BACK chain to the `else` at `:393-395` and answers NOT_DEALT_WITH.
	if (b == ButtonId::Save && mode_ == Mode::Manual) {
		const int32_t xx = (current_slice_ % 4) + (current_slice_ / 16) * 4; // `:307`
		const int32_t yy = (current_slice_ / 4) % 4;                         // `:308`
		if (ctx.held_pads.test(static_cast<uint8_t>(xx), static_cast<uint8_t>(yy))
		    && current_slice_ < num_manual_slices_) { // `:309`
			delete_current_slice();
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll}); // `:328-329`
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
		// `:371-392`, after dropping a held audition.
		if (ops_ != nullptr) {
			ops_->cancel_audition();
		}
		if (mode_ == Mode::Manual && ops_ != nullptr) {
			// `:376-379` renders the full-height waveform into `PadLEDs::image` on the way out --
			// legacy's parting frame for the transition it is about to run. It has no counterpart
			// here and needs none: the shell recomposes the grid bottom-up on the next tick from
			// whatever scenes are then on the stack, so there is no image to leave behind.
			// (`:376`'s own `RGB myImage[...]` is 432 bytes of dead stack -- declared, never read,
			// never written, and `:378` renders into `PadLEDs::image` instead. The REGION arm of
			// `renderMainPads` has the identical dead array at `:107`. Recorded in
			// the first entry of `src/scenes/README.md`'s "Defects this port found in shipping
			// firmware"; not fixed, and nothing to port.)
			ops_->kill_voices();        // `:380`
			ops_->restore_full_range(); // `:381-388`
		}
		// `:391` `close()`. `Remove` and not `Pop`, for the reason `shell/scene.h`'s note on
		// `Action::Kind::Pop` gives: a `Popup` this scene itself raised can legally be above it by
		// the time the router applies the action, and a blind `Pop` would take that instead.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kSlicerSceneKind});
		return r;
	}

	// `:393-395`: everything else falls through.
	return Result::unhandled();
}

Result SlicerScene::handle(const UiEvent& event, const UiContext& ctx) {
	switch (event.kind) {
	case UiEvent::Kind::Pad:
		// Legacy's third parameter is the raw velocity, which it names `on` and passes straight
		// through to `preview` (`Slicer::padAction`). The shell keeps `on == (velocity != 0)` invariantly
		// (`InputLowering::on_pad`, `shell/input_lowering.cpp`), so the velocity alone is
		// exactly what legacy is handed. Refused while a commit is out (our legacy's `slicePhase_` gate).
		if (committing_) {
			Result swallowed;
			swallowed.handled = true;
			return swallowed;
		}
		return pad_action(event.x, event.y, event.velocity);

	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		// `:279-281`, the top-of-function guard. `!on` is the release half; `currentUIMode !=
		// UI_MODE_NONE` is the commit gate, which is this scene's `committing_` -- legacy's other
		// UI modes are global state the shell does not have, and the ones a slicer could see
		// (a held encoder button, a held pad) are questions `UiContext` answers instead.
		if (!event.on || committing_) {
			return Result::unhandled();
		}
		return button_action(event.button, ctx);

	case UiEvent::Kind::Encoder:
		break;

	// The MIDI kinds (`shell/scene.h`) are declined here, as every non-timeline scene declines them: claiming one
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
	case deluge::ui::EncoderRole::Select:
		// Applied here, before the tick's render, so the frame that follows already shows the move.
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
	render_pads(f);
	// `graphicsRoutine`'s playhead: the newest voice's column on rows 7 to 4, in both modes, handed to
	// `PadLEDs::setTickSquares` whatever the flash cursor: painted with it Slow, flashed by the PIC with it Fast.
	playing_ = false;
	if (nav_.sample == nullptr || ops_ == nullptr) {
		return;
	}
	std::array<deluge::ui::grid::TickSquare, kDisplayHeight> per_row{};
	const std::optional<int32_t> pos = ops_->play_position();
	playing_ = pos.has_value();
	if (pos.has_value() && *pos >= nav_.xScroll && nav_.xZoom > 0) {
		const int64_t col = (*pos - nav_.xScroll) / nav_.xZoom;
		if (col < kDisplayWidth) {
			for (int32_t y = kDisplayHeight - 4; y < kDisplayHeight; y++) {
				per_row[y] = deluge::ui::grid::TickSquare{.col = static_cast<uint8_t>(col),
				                                          .colour = deluge::ui::grid::TickColour::Normal};
			}
		}
	}
	const bind::FlashCursor cursor = menu::host_runtime_facts().flash_cursor();
	if (cursor == bind::FlashCursor::Slow) {
		deluge::ui::grid::tick_squares(f, per_row, deluge::gui::colours::black);
	}
	deluge::ui::grid::flash_tick_squares(f, per_row, last_ticks_, cursor == bind::FlashCursor::Fast);
}

void SlicerScene::render_pads(deluge::ui::grid::GridFrame& f) {
	if (nav_.sample == nullptr) {
		return;
	}

	// **The region is legacy's fixed 16x8, not the frame's own extent, and there is no clamp.**
	// Every coordinate this scene draws at is a hard-coded constant in `renderMainPads`: the
	// squashed waveform goes to rows 4..7 (`slicer.cpp:117-121`), the slice marks to row 4
	// (`:122-129`), and the 64-slot grid is four pages of 4x4 laid out by `(i % 4) + (i / 16) * 4`
	// and `(i / 4) % 4` (`:131-149`), which has no meaning at any other size. `drawColBar` centres
	// its bars on `kDisplayHeight >> 1` too (`WaveformRenderer::drawColBar`,
	// `gui/waveform/waveform_renderer.cpp:208-239`), not on whatever height it was handed -- so
	// asking for exactly `kDisplayHeight` is what makes `ui::grid::waveform_full`, which centres on
	// `f.bounds().h >> 1`, agree with it.
	//
	// A frame that cannot grant all of that is handled by `GridFrame` itself: `pad()` outside
	// `visible()` paints nothing and `peek()` returns black, which is exactly what a clipped write
	// would have left behind, so there is no clamp of this function's own.

	// `renderFullScreen`'s scan (`gui/waveform/waveform_renderer.cpp:51-54`), and its early return:
	// a scan that could not load every cluster leaves the image **untouched** -- it returns before
	// its own clear. So a failed scan draws nothing here either, which is a divergence from
	// `MarkerScene` (which ignores the return) and a match for the routine this scene actually
	// ports.
	if (!deluge::sample::find_peaks_per_col(nav_.sample, nav_.xScroll, static_cast<uint64_t>(nav_.xZoom),
	                                        &nav_.renderData)) {
		return;
	}

	f.region(deluge::gfx::grid::Rect{0, 0, kDisplayWidth, kDisplayHeight}, [&](deluge::ui::grid::GridFrame& main) {
		services::paint_columns(main, nav_.renderData, nav_.sample->getFoundValueCentrePoint(),
		                        nav_.sample->getValueSpan());

		if (mode_ == Mode::Region) {
			// `:106-110`: the full-screen waveform, and nothing else.
			return;
		}

		// `:113-121`. Legacy renders the waveform full height into a SEPARATE image and then
		// averages row pairs into the top half, so every read is of the untouched full-height
		// render. Done in place that would be wrong -- row 6 reads rows 4 and 5, which rows 0 and 1
		// have already overwritten -- so the full-height render is snapshotted first.
		RGB src[kDisplayHeight][kDisplayWidth]{};
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				src[y][x] = main.peek(x, y);
			}
		}
		for (int32_t x = 0; x < kDisplayWidth; x++) {
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
