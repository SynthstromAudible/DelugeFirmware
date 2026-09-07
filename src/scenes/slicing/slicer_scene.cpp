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
#include "storage/owner.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

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
	// **Deliberately empty, and Task 10 is why.** Task 9 mapped this to `focusRegained` because
	// `closeUI` calls it on whatever is left on top after something above closes
	// (`gui/ui/ui.cpp:199`). That mapping is wrong, and the arms Task 10 added are what showed it:
	//
	// `Slicer` raises timed popups from four of its own arms -- `horizontalEncoderAction` (`:227`),
	// `verticalEncoderAction` (`:243`), `buttonAction`'s Y_ENC arm (`:301`) and, as a cancel,
	// `padAction` (`:503`, `:540`). In legacy a popup is a *display overlay* (`OLED::popupText`),
	// not a UI on `uiNavigationHierarchy`, so it never reaches `closeUI` and `focusRegained` never
	// runs for one. In the shell a popup **is** a scene: `Notices::show_popup_with_timeout` pushes
	// `SceneKind::Popup` (`shell/notices.cpp:301-325`) and `Notices::tick` removes it once
	// `kPopupTimeoutMs` -- 800 ms (`notices.cpp:31`) -- has passed (`notices.cpp:327-350`), and
	// `SceneStack::remove`'s `if (was_top && n_ > 0)` arm then calls `on_resume()` on the scene
	// beneath (`shell/scene_stack.cpp:108-113`).
	//
	// With the reset in place that meant: place forty slices by hand, tap a pad to hear one -- and
	// 800 ms later every slice is gone, because the scene's own popup expired. Measured, not
	// predicted: `sim/scenes_spec/slicer_scene_spec.cpp`'s "keeps its slice table when a popup it
	// raised itself expires" is the example, and it fails with this body restored.
	//
	// Nothing is lost by leaving it empty **today**. The `closeUI` edge legacy runs `focusRegained`
	// on needs a real UI to have been opened *over* the slicer, and `SlicerScene` pushes nothing --
	// the only scenes the shell puts above it are the three `input_transparent()` notice overlays,
	// which is exactly the case legacy runs nothing for.
	//
	// @warning **This is a placeholder, not the finished port, and it expires.** The moment either
	//          (a) `SceneStack` stops notifying on the removal of an `input_transparent()` scene, or
	//          (b) wiring pushes something real over the slicer -- a context menu, a MIDI-learn
	//          overlay, a sound-editor menu -- an empty body silently stops doing what legacy's
	//          `closeUI` -> `focusRegained` does (`gui/ui/ui.cpp:199`), and the slice table survives
	//          a menu that legacy would have wiped it for. **Restore the `on_enter` call here when
	//          the shell fix lands**, and delete this warning with it. Carried in
	//          `src/scenes/README.md`'s E4 roadmap table so the shell task has the instruction in the
	//          tree rather than in a review thread.
	//
	// @note The underlying asymmetry is the shell's and is reported upward, not patched here:
	//       *pushing* an `input_transparent()` overlay tells the scene beneath nothing (it never
	//       lost the input top -- `SceneStack::input_top` skips transparent scenes,
	//       `scene_stack.cpp:194`), but *removing* one tells it "you are back on top". `Scene` has
	//       no `on_pause` to pair with it either, so the edge cannot mean "you are back on top" --
	//       only "something above you vanished". And the same two arms *under*-notify symmetrically:
	//       `pop`/`remove` resume `stack_[n_ - 1]`, the raw top rather than the input top, so with
	//       `[slicer, popup, menu]` removing the menu notifies the POPUP and the slicer -- which
	//       genuinely regains the input top -- gets nothing.
	//
	//       **`MenuScene` is not immune either**, which an earlier draft of this comment got wrong.
	//       Its `go_up_on_resume` flag guards only the `ascend()` (`menu_scene.cpp:332-335`); the
	//       tail below it unconditionally resets `render_resp`, `value_changed`, `value_node`,
	//       `back_claimed` and `column_press` (`:337-341`) -- and `MenuScene` raises
	//       `Notice::Kind::Console` itself (`:1256`, and `menu/bindings.cpp:372`), which is
	//       `input_transparent()` and removed by `Notices::tick`. So a second live scene already
	//       takes the spurious edge, and `go_up_on_resume` is a port of `SoundEditor::focusRegained`'s
	//       "came back from a deeper UI" semantics (`gui/ui/sound_editor.cpp:279-289`), not a popup
	//       guard. It is a coincidence, not a pattern to copy.
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

// ---------------------------------------------------------------------------------------------
// The audition preview -- `Slicer::preview` / `runPreviewOp` (`slicer.cpp:409-444`)

void SlicerScene::preview(const PreviewRequest& req) {
	// `:418-432`, verbatim in structure and in type: snapshot the request (plain values -- nothing
	// points into `points_` past this call) and coalesce latest-wins onto one owner op, so a burst
	// of pad taps converges on whichever slice the finger actually landed on, and the op's
	// "is the sliced sample already resident?" check, the load it may trigger and the audition note
	// always run together on the worker rather than being split across a yield.
	if (!preview_coalescer_.request(req)) {
		return;
	}
	if (!deluge::storage::Owner::run(&SlicerScene::run_preview_op, this)) {
		// `:428-431`: the owner queue was full, so the op will never run and would wedge the
		// single-flight guard forever. Release it; the next `preview()` re-requests.
		preview_coalescer_.reset();
	}
}

void SlicerScene::run_preview_op(void* self) {
	// `:435-444`.
	auto* s = static_cast<SlicerScene*>(self);
	if (s->ops_ != nullptr) {
		s->ops_->audition(s->preview_coalescer_.current());
	}
	if (s->preview_coalescer_.complete().has_value()) {
		if (!deluge::storage::Owner::run(&SlicerScene::run_preview_op, self)) {
			s->preview_coalescer_.reset(); // `:441`
		}
	}
}

// ---------------------------------------------------------------------------------------------
// The commit -- `Slicer::runDoSliceOp` / `commitSlice` (`slicer.cpp:570-594`)

void SlicerScene::run_do_slice_op(void* self) {
	static_cast<SlicerScene*>(self)->commit_slice(); // `:570-573`
}

void SlicerScene::commit_slice() {
	if (ops_ != nullptr) {
		// `:576-589`: `doSlice(target.sliceCount)` and, for a MANUAL commit, the per-drum
		// start/end/transpose paint loop. They run together for the reason `runDoSliceOp`'s
		// docstring gives -- the loop indexes drums `doSlice` creates.
		ops_->commit(pending_commit_);
	}
	// `:591-593`. Released here and not inside `SliceOps::commit`, because every exit path of
	// `doSlice` -- its `getOut:` load-failure branch included -- has to reach it.
	committing_ = false;
}

// ---------------------------------------------------------------------------------------------
// Pads -- `Slicer::padAction` (`slicer.cpp:484-568`)

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
			// `:508-534` is the voice walk, which is not ported -- see `SliceOps::play_position`.
			// `:533`'s `if (voiceSample != nullptr)` is exactly "the walk found one", which is what
			// an engaged `nullopt` means here.
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

	// `:567` `return sampleBrowser.padAction(x, y, on)`. DIVERGENCE in spelling only: this layer may
	// not name `sampleBrowser` (`scenes-no-root-ui`), and it does not have to. Legacy hands the pad
	// to the UI it was opened over; the shell hands an unhandled event to the scene beneath, which
	// is that same browser. Falling through is the delegation, not a dropped input.
	return Result::unhandled();
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
		// `:334-368`. `inCardRoutine` has no counterpart here: it is a defer-and-retry gate whose
		// port is `InputLowering::set_sd_busy`, not a scene's business. See the class docstring's
		// "The commit does NOT need `RecorderScene`'s SD-routine guard".
		//
		// `:345-349`: snapshot the gesture -- plain values only, nothing pointing into `points_`,
		// `num_manual_slices_` or `num_clips_` survives past this point.
		pending_commit_.is_manual = mode_ == Mode::Manual;
		pending_commit_.slice_count = pending_commit_.is_manual ? num_manual_slices_ : num_clips_;
		if (pending_commit_.is_manual) {
			std::copy(std::begin(points_), std::end(points_), std::begin(pending_commit_.points));
			if (ops_ != nullptr) {
				ops_->kill_voices(); // `:350`
			}
		}

		// `:363` -- the gate, closed BEFORE dispatch and not inside the op, because the op ends by
		// popping this scene (and the browser beneath it) off the stack. It is what makes a second
		// SELECT_ENC and a BACK arriving during the op no-ops (`:279-281`).
		committing_ = true;
		if (!deluge::storage::Owner::run_or_inline(&SlicerScene::run_do_slice_op, this)) {
			// `:364-368`: dispatch dropped, so `run_do_slice_op` will never run and would leave the
			// scene wedged with no BACK and no SELECT_ENC forever. Release what was just closed.
			committing_ = false;
		}
		r.handled = true;
		return r;
	}

	if (b == ButtonId::Back) {
		// `:371-392`.
		if (mode_ == Mode::Manual && ops_ != nullptr) {
			// `:376-379` renders the full-height waveform into `PadLEDs::image` on the way out --
			// legacy's parting frame for the transition it is about to run. It has no counterpart
			// here and needs none: the shell recomposes the grid bottom-up on the next tick from
			// whatever scenes are then on the stack, so there is no image to leave behind.
			// (`:376`'s own `RGB myImage[...]` is 432 bytes of dead stack -- declared, never read,
			// never written, and `:378` renders into `PadLEDs::image` instead. The REGION arm of
			// `renderMainPads` has the identical dead array at `:107`. Recorded in
			// `src/scenes/README.md`'s ledger; not fixed, and nothing to port.)
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
		// through to `preview` (`:494`). The shell keeps `on == (velocity != 0)` invariantly
		// (`InputLowering::on_pad`, `shell/input_lowering.cpp:243-251`), so the velocity alone is
		// exactly what legacy is handed.
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

	case UiEvent::Kind::AllReleased:
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
	// would have left behind. An earlier cut of this function clamped both axes and then used
	// legacy's constants anyway, which made the clamp decorative -- the review caught it, and the
	// answer was to delete it rather than to spread it through arithmetic that is transcribed.

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
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		if (nav_.renderData.colStatus[x] != COL_STATUS_INVESTIGATED) {
			continue;
		}
		int32_t min24 = 0;
		int32_t max24 = 0;
		deluge::sample::col_bar_positions(x, &nav_.renderData, &min24, &max24, centre, span);
		peaks[static_cast<size_t>(x)] = {.min24 = min24, .max24 = max24, .valid = true};
	}

	f.region(deluge::gfx::grid::Rect{0, 0, kDisplayWidth, kDisplayHeight}, [&](deluge::ui::grid::GridFrame& main) {
		// `renderOneCol`'s monochrome arm -- `rgb` is `nullopt` on both of legacy's calls, so
		// `brightness` is 128 (`WaveformRenderer::renderOneCol`,
		// `gui/waveform/waveform_renderer.cpp:243-246`). `waveform_full`'s precondition, honoured.
		deluge::ui::grid::waveform_full(main, peaks, deluge::ui::grid::kWaveformBarBrightnessMono, std::nullopt);

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
