/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief `AudioClipScene` -- the port of `AudioClipView`'s render and navigation half. See
///        `audio_clip_scene.h` for the predicates, the playhead ruling and the sidebar finding.

#include "scenes/audio_clip/audio_clip_scene.h"

#include "gfx/grid/palette.h"
#include "ui/grid/columns.h"
#include "ui/grid/sidebar.h"
#include "ui/grid/waveform.h"
#include "ui/oled/misc.h"
#include "ui/oled/text.h"

#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_scan.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"

#include <algorithm>
#include <array>
#include <span>

// `util/functions.h` is not reachable from this layer (`check_layer_rules.py`'s
// `scenes-includes-outside` row lists no `util/`), and this is the one free function in it the OLED
// page needs. Declared rather than included -- the spelling `menu/nodes/output_items.cpp:47` already
// uses for the same symbol.
char const* getOutputTypeName(OutputType outputType, int32_t channel);

namespace deluge::scenes::audio_clip {

using deluge::ui::grid::GridFrame;
using deluge::ui::oled::OledFrame;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// @brief `renderMainPads`' fixed 16 main columns (`audio_clip_view.cpp:95-221` writes
///        `image[y][0 .. kDisplayWidth)` throughout).
constexpr int32_t kMainCols = kDisplayWidth;

/// @brief The colour `PadLEDs::prepareColour` substitutes for a `coloursArray` entry of 2, which is
///        the only non-zero value `graphicsRoutine` ever fills (`audio_clip_view.cpp:300-303`).
///
/// `TickColour::Muted` is unreachable from this scene for the same reason, so the `muted_colour`
/// argument `ui::grid::tick_squares` takes -- legacy's `mutedColourMenu.getRGB()`, a `gui/`
/// menu-item read this layer may not make -- can never be consulted. Black is passed, and this note
/// is why that is not a missing port.
constexpr RGB kUnreachableMutedColour = deluge::gui::colours::black;

} // namespace

AudioClipScene& audio_clip_scene() {
	static AudioClipScene scene;
	return scene;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle

void AudioClipScene::open(::AudioClip* clip, ::Song* song) {
	clip_ = clip;
	song_ = song;
	if (song_ != nullptr) {
		timeline_.adopt(*song_);
		// `adopt` copies `Song::tripletsOn`, but `supportsTriplets()` is a property of the VIEW
		// (`audio_clip_view.h:38` returns false) and is not in the song. Restored after every
		// adopt, so `in_triplets_view()` answers what `AudioClipView` answers.
		timeline_.supports_triplets = false;
	}
}

bool AudioClipScene::on_enter(const SceneId& id) {
	(void)id;
	// `AudioClipView::opened` (`audio_clip_view.cpp:68-76`). `uiNeedsRendering(this)` (`:70`) is the
	// shell's business in immediate mode, and `getCurrentClip()->onAutomationClipView = false`
	// (`:72`) is a flag `AutomationView` reads -- not ported, because `automationView` is on
	// `scenes-no-root-ui`'s forbidden list and nothing in this layer reads the flag either. What is
	// left is the tick-square latch and `focusRegained`, in legacy's own order.
	must_redraw_tick_squares_ = true;
	focus_regained();
	return true;
}

void AudioClipScene::on_resume() {
	focus_regained();
}

void AudioClipScene::focus_regained() {
	// `AudioClipView::focusRegained` (`audio_clip_view.cpp:78-89`); see the private declaration for
	// the four statements that have no counterpart in this layer.
	//
	// `ClipView::focusRegained` (`gui/views/clip_view.cpp:43-45`) forwards to
	// `ClipNavigationTimelineView::focusRegained` (`clip_navigation_timeline_view.cpp:26-28`),
	// whose whole body is the restore-point clear -- `TimelineState::focus_regained()`.
	timeline_.focus_regained();
	end_marker_visible_ = false;
	start_marker_visible_ = false;
	blink_on_ = false;
	blink_accum_ms_ = 0;
}

// ---------------------------------------------------------------------------------------------
// The two marker latches, and the three model questions that branch on them

void AudioClipScene::set_end_marker_visible(bool on) {
	if (end_marker_visible_ == on) {
		return;
	}
	end_marker_visible_ = on;
	// `padAction` restarts the blink whenever it raises a marker (`audio_clip_view.cpp:553-554`,
	// `:563-564`: `blinkOn = true;` then `setTimer(UI_SPECIFIC, kSampleMarkerBlinkTime)`), so the
	// bright half is always the one the user sees first.
	blink_on_ = on;
	blink_accum_ms_ = 0;
}

void AudioClipScene::set_start_marker_visible(bool on) {
	if (start_marker_visible_ == on) {
		return;
	}
	start_marker_visible_ = on;
	blink_on_ = on;
	blink_accum_ms_ = 0;
}

uint32_t AudioClipScene::max_length() const {
	// `AudioClipView::getMaxLength` (`audio_clip_view.cpp:857-862`).
	if (clip_ == nullptr) {
		return 0;
	}
	if (end_marker_visible_) {
		return static_cast<uint32_t>(clip_->loopLength) + 1;
	}
	return static_cast<uint32_t>(clip_->loopLength);
}

uint32_t AudioClipScene::max_zoom() const {
	// `AudioClipView::getMaxZoom` (`audio_clip_view.cpp:864-870`). The 1073741824 is legacy's own
	// literal, not `kMaxZoom`: it is the largest value that can be doubled inside an `int32_t`.
	if (clip_ == nullptr) {
		return 0;
	}
	int32_t max = clip_->getMaxZoom();
	if (end_marker_visible_ && max < 1073741824) {
		max <<= 1;
	}
	return static_cast<uint32_t>(max);
}

bool AudioClipScene::scrollable_and_zoomable() const {
	// `AudioClipView::setupScroll`'s guard (`audio_clip_view.cpp:851-853`).
	return clip_ != nullptr && clip_->currentlyScrollableAndZoomable();
}

// ---------------------------------------------------------------------------------------------
// Input

Result AudioClipScene::handle(const UiEvent& event, const UiContext& ctx) {
	(void)event;
	(void)ctx;
	// Task 11 is render and navigation state; every input arm of `AudioClipView` --
	// `buttonAction` (`audio_clip_view.cpp:324-470`), `padAction` (`:472-589`),
	// `selectEncoderAction` (`:726-739`), `horizontalEncoderAction` (`:787-796`) and
	// `verticalEncoderAction` (`:837-848`) -- is Task 12's citation list. Returning
	// `unhandled` (rather than swallowing) is what lets `services::GlobalControls` see the
	// events, which is exactly what legacy does for a root view that declines
	// (`hid/buttons.cpp:134-143`). See the `@warning` in the class docstring for the one
	// button where that hand-off is currently too generous.
	return Result::unhandled();
}

// ---------------------------------------------------------------------------------------------
// Time

void AudioClipScene::tick(uint32_t dt_ms) {
	// `AudioClipView::timerCallback` (`audio_clip_view.cpp:223-229`): toggle `blinkOn` (`:224`) and
	// re-arm (`:227`).
	// Legacy arms the timer only from `padAction`, when a marker goes up, and unsets it when one
	// goes down (`:353`, `:444`, `:460`), so the toggle runs only while a marker is visible.
	if (!end_marker_visible_ && !start_marker_visible_) {
		return;
	}
	blink_accum_ms_ += dt_ms;
	while (blink_accum_ms_ >= kMarkerBlinkMs) {
		blink_accum_ms_ -= kMarkerBlinkMs;
		blink_on_ = !blink_on_;
	}
}

std::optional<uint32_t> AudioClipScene::next_timer() const {
	// The playhead first: `graphicsRoutine` runs on every main-loop iteration
	// (`gui/ui/ui.cpp:47-51`), so its port is "repaint next tick" whenever the cursor could move.
	// The condition is `graphicsRoutine`'s own first gate, read positively
	// (`audio_clip_view.cpp:269-273`): with no playback, no active clip, or a count-in running, the
	// column is a constant 255 and nothing animates.
	const bool cursor_live = flash_cursor_ != FlashCursor::Off && clip_ != nullptr && song_ != nullptr
	                         && playbackHandler.playbackState != 0u && song_->isClipActive(clip_)
	                         && playbackHandler.ticksLeftInCountIn == 0u;
	if (cursor_live) {
		return 0u;
	}
	if (end_marker_visible_ || start_marker_visible_) {
		return kMarkerBlinkMs - std::min(blink_accum_ms_, kMarkerBlinkMs);
	}
	return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// The playhead

int32_t AudioClipScene::compute_tick_square() {
	// `AudioClipView::graphicsRoutine` (`audio_clip_view.cpp:262-310`), minus the paint.
	//
	// `:263-265`'s `UI_MODE_AUDIO_CLIP_COLLAPSING` early return, and `:270-271`'s two animation
	// modes, are `PadLEDs` transitions the port does not carry at all -- see the class docstring.
	if (clip_ == nullptr || song_ == nullptr) {
		return deluge::ui::grid::kNoTickSquare;
	}

	int32_t square = 0;
	if (playbackHandler.playbackState == 0u || !song_->isClipActive(clip_)
	    || playbackHandler.ticksLeftInCountIn != 0u) {
		// `:269-273`.
		square = deluge::ui::grid::kNoTickSquare;
	}
	else if (!playbackHandler.isEitherClockActive()
	         || (currentPlaybackMode == &arrangement && clip_->getCurrentlyRecordingLinearly())) {
		// Tempoless or arranger recording (`:275-284`).
		square = kMainCols - 1;
		if (clip_->getCurrentlyRecordingLinearly()) {
			// `:281`: force the waveform cache to be recomputed. This is why the routine runs from
			// `render_grid` and not from `tick` -- the scan below reads what this invalidates.
			clip_->renderData.xScroll = -1;
			// `:282`'s `needsRenderingDependingOnSubMode()` is a redraw request, which the shell
			// already grants every tick while the cursor is live (`next_timer`).
		}
	}
	else {
		// `:285-294`. `getTickSquare()` is `ClipView::getTickSquare` (`clip_view.cpp:354-389`),
		// whose state half is `TimelineState::tick_square` -- ported and measured by E4 task 8,
		// including the `xZoom`-for-`xScroll` defect in its restore arm.
		timeline::TimelineState::TickSquareGates gates{
		    .recording_linearly = clip_->getCurrentlyRecordingLinearly(),
		    // `(!currentUIMode || currentUIMode == UI_MODE_AUDITIONING) && getCurrentUI() == this`
		    // (`clip_view.cpp:361-362`). The port carries no UI modes on this scene, so the first
		    // half is true; the second has no counterpart -- a `Scene` cannot ask whether it is the
		    // stack's input top (`Scene::on_resume` fires on the edge, and `shell/scene.h` records
		    // that there is deliberately no `on_pause` dual to close the bracket). So this reads
		    // true where legacy would read false with something open over the clip view, and the
		    // difference is confined to the auto-extending-linear-recording follow.
		    .ui_idle = true,
		    .clip_unarmed = clip_->armState == ArmState::OFF,
		    .max_length = max_length(),
		};
		const timeline::TimelineState::TickSquare tick =
		    timeline_.tick_square(static_cast<int32_t>(clip_->getLivePos()), gates);
		square = tick.square;
		if (square < 0 || square >= kMainCols) {
			square = deluge::ui::grid::kNoTickSquare;
		}
	}

	// `:296-309`. The latch is `AudioClipView`'s own two private fields, which task 8's table
	// assigns here by name.
	if (flash_cursor_ != FlashCursor::Off && (square != last_tick_square_ || must_redraw_tick_squares_)) {
		last_tick_square_ = square;
		must_redraw_tick_squares_ = false;
	}
	return last_tick_square_;
}

// ---------------------------------------------------------------------------------------------
// Rendering

::Sample* AudioClipScene::current_sample() const {
	// `getSample()` (`audio_clip_view.cpp:60-66`).
	if (clip_ == nullptr) {
		return nullptr;
	}
	if (clip_->getCurrentlyRecordingLinearly()) {
		return clip_->recorder != nullptr ? clip_->recorder->sample : nullptr;
	}
	return static_cast<::Sample*>(clip_->sampleHolder.audioFile);
}

void AudioClipScene::render_grid(GridFrame& f) {
	// `graphicsRoutine` before the render, as the main loop runs it (`deluge.cpp`'s loop calls
	// `UI::graphicsRoutine` before `doAnyPendingUIRendering`), and because its middle arm's
	// `renderData.xScroll = -1` has to land before the scan.
	const int32_t tick_col = compute_tick_square();

	::Sample* const sample = current_sample();
	// `renderMainPads`' two "just clear the display" arms (`:105-111`, `:113-120`): no sample, or no clip. The
	// frame arrives cleared, so there is nothing to erase -- but the two arms still `return`, which
	// means no waveform, no grey, no markers and (legacy's driver still substitutes) the cursor.
	if (sample != nullptr && clip_ != nullptr) {
		const int32_t scroll = timeline_.x_scroll;
		const uint32_t zoom = timeline_.x_zoom;

		// `:125-131`. `divide_round_negative` is transcribed in `scenes/timeline/timeline.h`
		// because `util/functions.h` is not reachable from this layer.
		const int32_t end_square =
		    timeline::divide_round_negative(clip_->loopLength - scroll - 1, static_cast<int32_t>(zoom == 0 ? 1 : zoom));
		const int32_t start_square =
		    timeline::divide_round_negative(0 - scroll, static_cast<int32_t>(zoom == 0 ? 1 : zoom));

		int64_t scroll_samples = 0;
		int64_t zoom_samples = 0;
		clip_->getScrollAndZoomInSamples(scroll, static_cast<int32_t>(zoom), &scroll_samples, &zoom_samples);

		const RGB rgb = clip_->getColour();

		// `:140-145`: while the end marker blinks off, the waveform stops one column earlier so the
		// dim red marker column is not painted over.
		int32_t visible_end = end_square + 1;
		if (end_marker_visible_ && blink_on_) {
			visible_end--;
		}
		const int32_t x_end = std::min(kMainCols, visible_end);

		// `:147-148`'s `waveformRenderer.renderFullScreen`, split into its two halves: the scan
		// (model) and the paint (toolkit).
		//
		// The scan's failure is legacy's `:151-154`: `renderFullScreen` returns **before its own
		// clear** (`gui/waveform/waveform_renderer.cpp:51-54`), so a frame it could not load leaves
		// the image untouched and legacy asks to come back later. The port draws nothing for the
		// same reason -- `SlicerScene::render_grid` makes the identical call.
		if (deluge::sample::find_peaks_per_col(sample, scroll_samples, static_cast<uint64_t>(zoom_samples),
		                                       &clip_->renderData, clip_->recorder)) {
			std::array<deluge::ui::grid::PeakCol, kMainCols> peaks{};
			const int32_t centre = sample->getFoundValueCentrePoint();
			const int32_t span = sample->getValueSpan();
			const bool reversed = clip_->sampleControls.isCurrentlyReversed();
			for (int32_t x = 0; x < x_end; x++) {
				// `renderOneCol`'s own source mapping (`waveform_renderer.cpp:243`): the reversal
				// picks which SOURCE column an output column reads, which is why
				// `ui::grid::waveform_full` takes no `reversed` flag (its header says so).
				const int32_t src = reversed ? (kMainCols - 1 - x) : x;
				if (clip_->renderData.colStatus[src] != COL_STATUS_INVESTIGATED) {
					continue;
				}
				int32_t min24 = 0;
				int32_t max24 = 0;
				deluge::sample::col_bar_positions(src, &clip_->renderData, &min24, &max24, centre, span);
				peaks[static_cast<size_t>(x)] = {.min24 = min24, .max24 = max24, .valid = true};
			}

			f.region(deluge::gfx::grid::Rect{0, 0, kMainCols, f.bounds().h}, [&](GridFrame& main) {
				// `renderOneCol`'s coloured arm: `rgb` is engaged on this call site
				// (`audio_clip_view.cpp:148`), so `brightness` is 256
				// (`waveform_renderer.cpp:243`). `waveform_full`'s precondition, honoured.
				deluge::ui::grid::waveform_full(main, peaks, deluge::ui::grid::kWaveformBarBrightnessColoured, rgb);

				// `:157-218`, the "grey regions + flashing columns" loop. `drawUndefinedArea` is
				// the caller's fourth argument and defaults to true (`audio_clip_view.h:32`);
				// `doAnyPendingGridRendering` calls the three-argument form
				// (`gui/ui/ui.cpp:329`), so on the device it is always true.
				for (int32_t y = 0; y < main.bounds().h; y++) {
					// -------- END marker (`:160-181`) --------
					if (end_square < kMainCols) {
						if (end_square >= 0 && end_marker_visible_) {
							(void)main.pad(end_square, y,
							               blink_on_ ? deluge::gui::colours::red : deluge::gui::colours::red_dull);
						}
						int32_t x = end_square + 1;
						if (x < kMainCols) {
							x = std::max<int32_t>(x, 0);
							for (int32_t g = x; g < kMainCols; g++) {
								(void)main.pad(g, y, deluge::gui::colours::grey);
							}
						}
					}

					// -------- START marker (`:183-216`) --------
					// Legacy's own commented-out grey fill is left out; what remains live is the
					// green marker column when it is on screen, and a whole-width grey fill when
					// the start marker has scrolled off the RIGHT of the display.
					if (start_square >= 0) {
						if (start_square < kMainCols) {
							if (start_marker_visible_) {
								(void)main.pad(start_square, y,
								               blink_on_ ? deluge::gui::colours::green
								                         : deluge::gui::colours::green.dim());
							}
						}
						else {
							for (int32_t g = 0; g < kMainCols; g++) {
								(void)main.pad(g, y, deluge::gui::colours::grey);
							}
						}
					}
				}
			});
		}
	}

	// `AudioClipView::renderSidebar` (`:231-260`): both columns black (`:244-245`). The macro arm is not ported;
	// see the class docstring's warning.
	const std::array<RGB, kDisplayHeight> black{};
	deluge::ui::grid::sidebar_columns(f, black, black);

	// `graphicsRoutine`'s `PadLEDs::setTickSquares` (`:305`), which the driver applies to the whole
	// rendered image on its way out (`PadLEDs::prepareColour`, `hid/led/pad_leds.cpp:283-298`) --
	// hence last, and over the root frame rather than the main-column region.
	if (flash_cursor_ == FlashCursor::Slow && tick_col != deluge::ui::grid::kNoTickSquare) {
		// `:300-303`: every row gets the same column, and the colour is red (2) while recording
		// linearly, normal (0) otherwise.
		const deluge::ui::grid::TickColour colour = (clip_ != nullptr && clip_->getCurrentlyRecordingLinearly())
		                                                ? deluge::ui::grid::TickColour::Red
		                                                : deluge::ui::grid::TickColour::Normal;
		std::array<deluge::ui::grid::TickSquare, kDisplayHeight> per_row{};
		per_row.fill(deluge::ui::grid::TickSquare{.col = static_cast<uint8_t>(tick_col), .colour = colour});
		deluge::ui::grid::tick_squares(f, per_row, kUnreachableMutedColour);
	}
}

void AudioClipScene::render_oled(SurfaceRole role, OledFrame& f) {
	(void)role;
	// `AudioClipView::renderOLED` (`audio_clip_view.cpp:91-93`) is one call:
	// `view.displayOutputName(getCurrentOutput(), false, getCurrentClip())`
	// (`gui/views/view.cpp:1992-2021`), which flattens the output into five values and hands them
	// to `View::drawOutputNameFromDetails` (`:2025-2170`). `doBlink` is **false** here, so that
	// function's whole first half -- the `IndicatorLED` block at `:2027-2074` -- is skipped.
	if (clip_ == nullptr || clip_->output == nullptr) {
		return;
	}

	// `displayOutputName`'s AUDIO arm (`view.cpp:2014-2016`): `channel` is the output's mode, and
	// `editedByUser` stays at its initialiser because the `!= OutputType::AUDIO` branch is not
	// taken. An `AudioClip`'s output is always an `AudioOutput` (`AudioClip::setOutput` is only
	// ever handed one), so the type is `OutputType::AUDIO`.
	const auto* out = static_cast<const AudioOutput*>(clip_->output);
	const int32_t channel = static_cast<int32_t>(out->mode);

	// **`name_empty` is deliberately false even when the name IS empty**, and this is a real
	// difference between the widget and this call site. `ui::oled::output_name`'s `name_empty`
	// branch draws a SLOT (`ui/oled/misc.cpp:63-64`), which is legacy's substitution for an unnamed
	// **MIDI or CV** output (`view.cpp:2145-2170`). `drawOutputNameFromDetails`' name block is one
	// `if (!isNameEmpty) ... else if (outputType == MIDI_OUT) ... else if (outputType == CV)`
	// chain (`:2101-2168`), and an `OutputType::AUDIO` output with no name matches **none** of the
	// three -- so legacy draws the type text and nothing else. Passing the empty name with
	// `name_empty = false` is what reproduces that: the widget takes its draw-the-name branch and
	// draws an empty string. Measured against legacy in
	// `sim/scenes_spec/audio_clip_scene_spec.cpp`'s "unnamed output" example, which fails with
	// `name_empty = out->name.empty()`.
	const bool name_is_empty = out->name.empty();
	deluge::ui::oled::OutputNameData data{
	    .type_text = getOutputTypeName(OutputType::AUDIO, channel),
	    .name = out->name,
	    .slot = 0,
	    .sub_slot = -1,
	    .name_empty = false,
	    .edited = true,
	};
	deluge::ui::oled::output_name(f, f.bounds(), data);

	// `:2128-2143`, the clip's own second line. It is drawn only inside the `!isNameEmpty` arm and
	// only when a `clip` was passed -- `AudioClipView` passes one. `ui::oled::output_name`'s header
	// says this line is the scene's, "being model data this struct does not carry".
	//
	// The side-scroller legacy also arms for it (`:2140-2142`) is not ported, for the reason
	// `ui/oled/misc.h` gives for the name's own scroller: it is `ui::oled::scroll_text`'s job and
	// needs state the widget does not own.
	if (name_is_empty) {
		return;
	}

	// The y is legacy's `yPos = yPos + 14` (`:2137`) over the name's own y, which
	// `ui::oled::output_name` picks from the same profile value this does
	// (`ui/oled/misc.cpp:52-58`).
	const bool tall = f.style.menu_base_y == deluge::ui::kMenuBaseY64;
	const int32_t name_y = f.bounds().y + (tall ? deluge::ui::kOutputNameY64 : deluge::ui::kOutputNameY48);

	// `:2130-2136`: "Section N" when the clip has no name, "N: name" when it has one, with the
	// section counted from one. Formatted by hand -- `deluge::string::fromInt` lives in `util/`,
	// which this layer may not include, and there is no allocation in library code (plan
	// constraint 6).
	std::array<char, 32> buffer{};
	size_t len = 0;
	const auto append = [&](std::string_view s) {
		for (const char c : s) {
			if (len + 1 < buffer.size()) {
				buffer[len++] = c;
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

	const uint32_t section = static_cast<uint32_t>(clip_->section) + 1;
	if (clip_->name.empty()) {
		append("Section ");
		append_int(section);
	}
	else {
		append_int(section);
		append(": ");
		append(clip_->name);
	}

	const int32_t row_h = deluge::ui::oled::text_height_for(f.style.row_font);
	deluge::ui::oled::text(
	    f, deluge::gfx::oled::Rect{f.bounds().x, name_y + 14, f.bounds().w, row_h},
	    std::string_view{buffer.data(), len},
	    deluge::ui::oled::TextStyle{.font = f.style.row_font, .align = deluge::ui::oled::Align::Centre});
}

} // namespace deluge::scenes::audio_clip
