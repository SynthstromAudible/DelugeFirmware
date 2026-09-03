/// @file
/// @brief `MarkerScene`. See `marker_scene.h`.

#include "scenes/marker/marker_scene.h"

#include "ui/core/metrics.h"
#include "ui/grid/frame.h"
#include "ui/oled/sample_position.h"
#include "ui/oled/text.h"

#include "gfx/grid/palette.h"
#include "gfx/oled/icons.h"

#include "gui/l10n/strings.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/instrument/instrument.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/sample/waveform_scan.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace deluge::scenes::marker {

using deluge::l10n::String;
using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

/// `SampleMarkerEditor::kInvalidColumn` (`sample_marker_editor.h:75`): the column a marker that is
/// not set at all reports. Deliberately the most negative `int32_t` rather than -1, so every
/// `colOnScreen <= x` comparison in `pad_action` refuses on it without a separate test.
constexpr int32_t kInvalidColumn = -2147483648;

/// `divide_round_negative` (`util/functions.cpp`), inlined: `src/scenes` may not include `util/`
/// (`check_layer_rules.py`, `scenes-includes-outside`) and this is the only thing it was wanted
/// for. Floors toward minus infinity, which is what makes a marker one sample left of the view
/// report column -1 rather than 0.
int32_t divide_round_negative(int32_t dividend, int32_t divisor) {
	if (dividend >= 0) {
		return static_cast<int32_t>(static_cast<uint32_t>(dividend) / static_cast<uint32_t>(divisor));
	}
	return -static_cast<int32_t>(static_cast<uint32_t>(-dividend - 1) / static_cast<uint32_t>(divisor)) - 1;
}

int32_t idx(MarkerType t) {
	return static_cast<int32_t>(t);
}

/// `sample_marker_editor.cpp:1065-1083`, in `MarkerType` order.
const std::array<RGB, kNumMarkerTypes> kMarkerColours{
    deluge::gui::colours::green,
    deluge::gui::colours::cyan,
    deluge::gui::colours::magenta,
    deluge::gui::colours::red,
};

/// The body's first line: legacy's `OLED_MAIN_TOPMOST_PIXEL + 17` (`:1137`). The frame's origin
/// already accounts for the bezel, so this is `title_height + 5` -- and reading it off the style
/// rather than hard-coding 17 is what keeps the two lines under the separator on a panel whose
/// title face is taller.
int32_t body_y(const deluge::ui::oled::OledFrame& f) {
	return f.style.title_height + 5;
}

} // namespace

std::span<const RGB> marker_colours() {
	return std::span<const RGB>{kMarkerColours};
}

// ---------------------------------------------------------------------------------------------
// Context

MultisampleRange* MarkerScene::multi_range() const {
	if (ctx_.is_audio_clip) {
		return nullptr;
	}
	return static_cast<MultisampleRange*>(ctx_.range);
}

SampleHolder* MarkerScene::holder() const {
	// `getCurrentSampleHolder` (`sample_marker_editor.cpp:62`): the audio clip's own holder, else
	// the selected multisample range's. Legacy asks `getCurrentAudioClip()`; the context has
	// already resolved which clip is current, so this asks it.
	if (ctx_.is_audio_clip) {
		auto* clip = static_cast<AudioClip*>(ctx_.clip);
		return clip != nullptr ? &clip->sampleHolder : nullptr;
	}
	MultisampleRange* range = multi_range();
	return range != nullptr ? &range->sampleHolder : nullptr;
}

bool MarkerScene::loop_locked() const {
	// `isLoopLocked()` (`:79`).
	MultisampleRange* range = multi_range();
	return range != nullptr && range->sampleHolder.loopLocked;
}

MarkerType MarkerScene::reverse_remap(MarkerType type) const {
	// `SampleMarkerEditor::reverseRemap` (`:83`).
	//
	// DIVERGENCE, small but labelled like the rest: legacy's `getCurrentSampleControls()` (`:71`)
	// dereferences `soundEditor.currentSource` with no null test, so a context that resolved no
	// source would crash there. `context_for` leaves `sample_controls` null on exactly the contexts
	// this scene cannot open on anyway (`can()` refuses them first), so the guard is unreachable
	// today; it reads `false` -- "not reversed" -- rather than crashing if that ever stops being
	// true, which is the same survivable-spelling choice `Descriptor::can` made in `BrowserScene`.
	// The same guard appears at `write_value`'s `claimClusterReasons` call and in
	// `allow_extra_scroll_right`, for the same reason.
	const bool reversed = ctx_.sample_controls != nullptr && ctx_.sample_controls->reversed;
	if (!reversed) {
		return type;
	}
	switch (type) {
	case MarkerType::NOT_AVAILABLE:
	case MarkerType::NONE:
		return type;
	case MarkerType::START:
		return MarkerType::END;
	case MarkerType::LOOP_START:
		return MarkerType::LOOP_END;
	case MarkerType::LOOP_END:
		return MarkerType::LOOP_START;
	case MarkerType::END:
		return MarkerType::START;
	}
	return type;
}

bool MarkerScene::can(const menu::MenuContext& ctx) {
	// `SampleMarkerEditor::opened`'s refusal (`:121-124`), plus the holder test legacy gets from
	// `LoopPoint::checkPermissionToBeginSession` before it ever opens.
	SampleHolder* h = nullptr;
	if (ctx.is_audio_clip) {
		auto* clip = static_cast<AudioClip*>(ctx.clip);
		h = clip != nullptr ? &clip->sampleHolder : nullptr;
	}
	else if (ctx.range != nullptr) {
		h = &static_cast<MultisampleRange*>(ctx.range)->sampleHolder;
	}
	return h != nullptr && h->audioFile != nullptr;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle

void MarkerScene::open(const menu::MenuContext& ctx) {
	ctx_ = ctx;
}

bool MarkerScene::on_enter(const SceneId& id) {
	if (!can(ctx_)) {
		return false;
	}

	// `SceneId::arg` is the `MarkerType` to start on. Legacy's two openers set
	// `sampleMarkerEditor.markerType` immediately before `openUI` (`sample/loop_point.cpp:69`,
	// `audio_clip/sample_marker_editor.cpp`); here it travels with the id instead of through a
	// member the caller has to remember to write.
	const auto requested = static_cast<MarkerType>(static_cast<int16_t>(id.arg));
	marker_type_ = (requested == MarkerType::START || requested == MarkerType::LOOP_START
	                || requested == MarkerType::LOOP_END || requested == MarkerType::END)
	                   ? requested
	                   : MarkerType::START;

	nav_.sample = static_cast<Sample*>(holder()->audioFile);
	nav_.opened(holder()); // `:126`

	blink_phase_ = 0; // `:128`
	blink_accum_ms_ = 0;
	holding_ = false;
	press_x_ = -1;
	press_y_ = -1;
	marker_on_screen_ = false;
	return true;
}

void MarkerScene::on_exit() {
	// Legacy has no `close()` hook of its own -- `exitUI` just calls `close()` (`:680`). The one
	// thing worth dropping is the held-marker gesture, which must not survive into the next entry:
	// `currentUIMode` is a global legacy clears on the way out of every UI, and this scene's copy
	// of it is a member.
	holding_ = false;
	press_x_ = -1;
	press_y_ = -1;
}

// ---------------------------------------------------------------------------------------------
// Marker positions

void MarkerScene::cols_on_screen(MarkerColumn* cols) const {
	// `SampleMarkerEditor::getColsOnScreen` (`:236`), with its two column mappings inlined:
	// `getStartColOnScreen` (`:218`) floors `(pos - xScroll) / xZoom`, and `getEndColOnScreen`
	// (`:223`) floors `(pos - 1 - xScroll) / xZoom` -- an end marker names the column its LAST
	// sample is in, which is why the two are not the same function.
	const SampleHolder* h = holder();
	const auto start_col = [this](int32_t pos) {
		return divide_round_negative(static_cast<int32_t>(pos - nav_.xScroll), static_cast<int32_t>(nav_.xZoom));
	};
	const auto end_col = [this](int32_t pos) {
		return divide_round_negative(static_cast<int32_t>(pos - 1 - nav_.xScroll), static_cast<int32_t>(nav_.xZoom));
	};

	cols[idx(MarkerType::START)].pos = static_cast<int32_t>(h->startPos);
	cols[idx(MarkerType::START)].colOnScreen = start_col(cols[idx(MarkerType::START)].pos);

	if (!ctx_.is_audio_clip) {
		const SampleHolderForVoice& sh = multi_range()->sampleHolder;
		cols[idx(MarkerType::LOOP_START)].pos = static_cast<int32_t>(sh.loopStartPos);
		cols[idx(MarkerType::LOOP_START)].colOnScreen = cols[idx(MarkerType::LOOP_START)].pos != 0
		                                                    ? start_col(cols[idx(MarkerType::LOOP_START)].pos)
		                                                    : kInvalidColumn;

		cols[idx(MarkerType::LOOP_END)].pos = static_cast<int32_t>(sh.loopEndPos);
		cols[idx(MarkerType::LOOP_END)].colOnScreen =
		    cols[idx(MarkerType::LOOP_END)].pos != 0 ? end_col(cols[idx(MarkerType::LOOP_END)].pos) : kInvalidColumn;
	}
	else {
		// An audio clip has no loop markers at all (`:255-261`).
		cols[idx(MarkerType::LOOP_START)].pos = 0;
		cols[idx(MarkerType::LOOP_START)].colOnScreen = kInvalidColumn;
		cols[idx(MarkerType::LOOP_END)].pos = 0;
		cols[idx(MarkerType::LOOP_END)].colOnScreen = kInvalidColumn;
	}

	cols[idx(MarkerType::END)].pos = static_cast<int32_t>(h->endPos);
	cols[idx(MarkerType::END)].colOnScreen = end_col(cols[idx(MarkerType::END)].pos);
}

int32_t start_pos_from_col(const deluge::sample::WaveformNav& nav, int32_t col) {
	return static_cast<int32_t>(nav.xScroll + col * nav.xZoom); // `:228`
}

int32_t end_pos_from_col(const deluge::sample::WaveformNav& nav, int32_t col) {
	return static_cast<int32_t>(nav.xScroll + (col + 1) * nav.xZoom); // `:232`
}

void MarkerScene::record_scroll_and_zoom() {
	// `SampleMarkerEditor::recordScrollAndZoom` (`:140`).
	if (marker_type_ != MarkerType::NONE) {
		SampleHolder* h = holder();
		h->waveformViewScroll = nav_.xScroll;
		h->waveformViewZoom = nav_.xZoom;
	}
}

bool MarkerScene::allow_extra_scroll_right() const {
	// `SampleMarkerEditor::shouldAllowExtraScrollRight` (`:1044`).
	if (marker_type_ == MarkerType::NONE || (ctx_.sample_controls != nullptr && ctx_.sample_controls->reversed)) {
		return false;
	}
	if (ctx_.is_audio_clip) {
		return true;
	}
	return ctx_.source != nullptr && ctx_.source->repeatMode == SampleRepeatMode::STRETCH;
}

void MarkerScene::set_loop_locked(bool locked) {
	// `loopLock`/`loopUnlock` (`:1225`, `:1231`) minus their popups.
	//
	// The two popups are NOT filed, and that is a limitation rather than a choice: legacy raises
	// `display->displayPopup("FREE")` / `("LOCK")` with **raw literals**, and `Action::Kind::Popup`
	// carries a `uint16_t` l10n id (`shell/scene.h`). Neither word is in `gui/l10n/strings.h`, so
	// filing them would mean inventing two string ids and their translation entries -- a change to
	// the l10n data rather than a port of the editor. The same argument `BrowserScene::Descriptor`
	// makes for its two literal titles, with the opposite conclusion available there because a
	// title is a `string_view` and a popup is not.
	MultisampleRange* range = multi_range();
	if (range != nullptr) {
		range->sampleHolder.loopLocked = locked;
	}
}

// ---------------------------------------------------------------------------------------------
// Writing

void MarkerScene::write_value(uint32_t value, MarkerType which) {
	// `SampleMarkerEditor::writeValue` (`:148`).
	if (which == MarkerType::NOT_AVAILABLE) {
		which = marker_type_;
	}

	bool audio_clip_active = false;
	if (ctx_.is_audio_clip) {
		auto* clip = static_cast<AudioClip*>(ctx_.clip);
		// `currentSong->isClipActive(...)` in legacy; the context carries the song, so the
		// `scenes-currentSong` rule is satisfied without changing what is asked.
		audio_clip_active = playbackHandler.isEitherClockActive() && ctx_.song != nullptr
		                    && ctx_.song->isClipActive(clip) && clip->voiceSample != nullptr;
		clip->unassignVoiceSample(false);
	}

	SampleHolder* h = holder();

	if (which == MarkerType::START) {
		h->startPos = value;
	}
	else if (which == MarkerType::LOOP_START) {
		SampleHolderForVoice& sh = multi_range()->sampleHolder;
		if (sh.loopLocked) {
			const uint32_t intended_end = value + static_cast<uint32_t>(sh.loopLength());
			if (uint64_t{intended_end} <= sh.endPos) {
				sh.loopStartPos = value;
				sh.loopEndPos = intended_end;
			}
		}
		else {
			sh.loopStartPos = value;
		}
	}
	else if (which == MarkerType::LOOP_END) {
		SampleHolderForVoice& sh = multi_range()->sampleHolder;
		if (sh.loopLocked) {
			const int32_t intended_start = static_cast<int32_t>(value) - sh.loopLength();
			// pos == 0 would disable the loop, so the smallest legal start position is sample 1
			if (intended_start >= 1 && static_cast<uint64_t>(intended_start) >= sh.startPos) {
				sh.loopEndPos = value;
				sh.loopStartPos = intended_start;
			}
		}
		else {
			sh.loopEndPos = value;
		}
	}
	else if (which == MarkerType::END) {
		h->endPos = value;
	}

	// Marker prefetch must never block the UI on a card read -- enqueue and let the loader fill it.
	h->claimClusterReasons(ctx_.sample_controls != nullptr && ctx_.sample_controls->reversed, CLUSTER_ENQUEUE);

	// The model-side tail (`:203-215`) is `menu::marker_written`, which lives beside
	// `model_stack_for` because both arms need a `ModelStack` they can call a member on and this
	// file may not include `model/model_stack.h` (`scenes-model-stack-outside-context`).
	menu::marker_written(ctx_, which, audio_clip_active);
}

// ---------------------------------------------------------------------------------------------
// Input

void MarkerScene::select_encoder(int32_t offset) {
	// `SampleMarkerEditor::selectEncoderAction` (`:268`). Legacy's `currentUIMode` guard (`:269`)
	// has no port: the modes it excludes are global states this layer cannot read, and the one it
	// permits (`UI_MODE_AUDITIONING`) is the ordinary case.
	MarkerColumn cols[kNumMarkerTypes];
	cols_on_screen(cols);

	const int32_t old_col = cols[idx(marker_type_)].colOnScreen;
	const int32_t old_pos = cols[idx(marker_type_)].pos;
	int32_t new_col = old_col + offset;

	// Make sure we don't drive one marker into the other
	for (int32_t c = 0; c < kNumMarkerTypes; c++) {
		if (c == idx(marker_type_)) {
			continue;
		}
		if (cols[c].colOnScreen == old_col || cols[c].colOnScreen == new_col) {
			return;
		}
	}

	int32_t new_pos =
	    (marker_type_ < MarkerType::LOOP_END) ? start_pos_from_col(nav_, new_col) : end_pos_from_col(nav_, new_col);
	if (new_pos < 0) {
		new_pos = 0;
	}

	if (offset >= 0) {
		if (marker_type_ == MarkerType::END && allow_extra_scroll_right()) {
			if (new_pos < old_pos) {
				return;
			}
		}
		else {
			if (new_pos > static_cast<int32_t>(nav_.sample->lengthInSamples)) {
				new_pos = static_cast<int32_t>(nav_.sample->lengthInSamples);
			}
		}
	}

	write_value(static_cast<uint32_t>(new_pos));

	// If marker was on-screen...
	if (old_col >= 0 && old_col < kDisplayWidth) {
		cols_on_screen(cols);
		// It might have changed, and despite having a new_col variable above, that's only our
		// desired value - we might have run into the end of the sample
		new_col = cols[idx(marker_type_)].colOnScreen;

		// But isn't anymore...
		if (new_col < 0 || new_col >= kDisplayWidth) {
			nav_.xScroll += nav_.xZoom * offset;
			if (nav_.xScroll < 0) {
				nav_.xScroll = 0; // Shouldn't happen...
			}
			record_scroll_and_zoom();
		}
	}

	blink_phase_ = 0;
	blink_accum_ms_ = 0;
}

void MarkerScene::pad_action(int32_t x, int32_t y, bool on) {
	// `SampleMarkerEditor::padAction`'s main-pad arms (`:367-641`). The two sidebar arms are not
	// here: the audition column (`:353`) delegates to `instrumentClipView`, a root UI this layer
	// may not name, and the mute column (`:361`) exits -- which `handle()` files as an action,
	// since only it can return one.
	if (!on) {
		// Release (`:635-641`): the held-marker gesture ends when its own pad goes up.
		if (holding_ && x == press_x_ && y == press_y_) {
			holding_ = false;
		}
		return;
	}

	MarkerColumn cols[kNumMarkerTypes];
	cols_on_screen(cols);

	// See which one we pressed (`:381-394`), including legacy's toggle between two markers that
	// share a column.
	MarkerType marker_pressed = MarkerType::NONE;
	for (int32_t m = 0; m < kNumMarkerTypes; m++) {
		if (cols[m].colOnScreen == x) {
			if (marker_pressed != MarkerType::NONE) {
				if (static_cast<MarkerType>(m) != marker_type_) {
					marker_pressed = static_cast<MarkerType>(m);
				}
			}
			else {
				marker_pressed = static_cast<MarkerType>(m);
			}
		}
	}

	if (holding_) {
		// `:399-529`. An audio clip has no loop markers, so legacy's whole held-marker arm sits
		// inside `if (getCurrentClip()->type == ClipType::INSTRUMENT)` (`:401`) and simply falls
		// out of the bottom on an audio clip -- leaving `currentUIMode` set and doing nothing.
		if (ctx_.is_audio_clip) {
			return;
		}

		// See which one we were holding down (`:403-408`).
		MarkerType marker_held = MarkerType::NONE;
		for (int32_t m = 0; m < kNumMarkerTypes; m++) {
			if (cols[m].colOnScreen == press_x_) {
				marker_held = static_cast<MarkerType>(m);
			}
		}

		if (marker_held == MarkerType::START) {
			// Unless we actually just tapped the already existing loop point (`:418-426`): remove it.
			if (x == cols[idx(MarkerType::LOOP_START)].colOnScreen) {
				marker_type_ = MarkerType::LOOP_START;
				set_loop_locked(false);
				write_value(0);
				marker_type_ = MarkerType::START;
				return;
			}
			// Limit position (`:429-438`).
			if (cols[idx(MarkerType::START)].colOnScreen >= x) {
				return;
			}
			if (multi_range()->sampleHolder.loopEndPos != 0 && cols[idx(MarkerType::LOOP_END)].colOnScreen <= x) {
				return;
			}
			if (cols[idx(MarkerType::END)].colOnScreen <= x) {
				return;
			}
			const int32_t new_value = start_pos_from_col(nav_, x);
			// Loop start and end points are not allowed to be further right than the waveform length
			if (new_value >= static_cast<int32_t>(nav_.sample->lengthInSamples)) {
				return;
			}
			marker_type_ = MarkerType::LOOP_START;
			holding_ = false;
			blink_phase_ = 0;
			blink_accum_ms_ = 0;
			write_value(static_cast<uint32_t>(new_value));
			return;
		}

		if (marker_held == MarkerType::END) {
			if (x == cols[idx(MarkerType::LOOP_END)].colOnScreen) {
				marker_type_ = MarkerType::LOOP_END;
				set_loop_locked(false);
				write_value(0);
				marker_type_ = MarkerType::END;
				return;
			}
			if (cols[idx(MarkerType::START)].colOnScreen >= x) {
				return;
			}
			if (cols[idx(MarkerType::LOOP_START)].colOnScreen >= x) {
				return; // Will be a big negative number if inactive
			}
			if (cols[idx(MarkerType::END)].colOnScreen <= x) {
				return;
			}
			const int32_t new_value = end_pos_from_col(nav_, x);
			if (new_value >= static_cast<int32_t>(nav_.sample->lengthInSamples)) {
				return;
			}
			marker_type_ = MarkerType::LOOP_END;
			holding_ = false;
			blink_phase_ = 0;
			blink_accum_ms_ = 0;
			write_value(static_cast<uint32_t>(new_value));
			return;
		}

		if (marker_held == MarkerType::LOOP_START && marker_pressed == MarkerType::LOOP_END) {
			// Toggle loop lock (`:480-489`).
			set_loop_locked(!multi_range()->sampleHolder.loopLocked);
			return;
		}

		if (marker_held == MarkerType::LOOP_START) {
			// Or if a loop point and they pressed the end marker, remove the loop point (`:492-509`).
			if (x == cols[idx(MarkerType::START)].colOnScreen) {
				set_loop_locked(false);
				write_value(0);
				marker_type_ = MarkerType::START;
				holding_ = false;
				blink_phase_ = 1;
				blink_accum_ms_ = 0;
			}
			return;
		}

		if (marker_held == MarkerType::LOOP_END) {
			if (x == cols[idx(MarkerType::END)].colOnScreen) {
				set_loop_locked(false);
				write_value(0);
				marker_type_ = MarkerType::END;
				holding_ = false;
				blink_phase_ = 1;
				blink_accum_ms_ = 0;
			}
			return;
		}

		// Legacy falls out of the bottom here with `markerHeld == NONE` and clears the mode
		// (`:525-527`) before writing an UNINITIALISED `value` -- the one path in `padAction` that
		// reads `value` without having assigned it. Refusing is the port; see the report.
		holding_ = false;
		return;
	}

	// Or if user not already holding a marker down (`:532-627`)...
	if (marker_pressed >= MarkerType::START) {
		// If we tapped a marker (`:535-541`).
		blink_phase_ = (marker_type_ == marker_pressed) ? 0 : 1;
		blink_accum_ms_ = 0;
		marker_type_ = marker_pressed;
		holding_ = true;
		press_x_ = static_cast<int8_t>(x);
		press_y_ = static_cast<int8_t>(y);
		return;
	}

	// Otherwise, move the current marker to where we tapped (`:544-626`). Make sure it doesn't go
	// past any other markers it shouldn't.
	if (marker_type_ == MarkerType::START) {
		if (cols[idx(MarkerType::LOOP_START)].pos != 0 && cols[idx(MarkerType::LOOP_START)].colOnScreen <= x) {
			return;
		}
		if (cols[idx(MarkerType::LOOP_END)].pos != 0 && cols[idx(MarkerType::LOOP_END)].colOnScreen <= x) {
			return;
		}
		if (cols[idx(MarkerType::END)].colOnScreen <= x) {
			return;
		}
	}
	else if (marker_type_ == MarkerType::LOOP_START) {
		if (cols[idx(MarkerType::START)].colOnScreen >= x) {
			return;
		}
		if (cols[idx(MarkerType::LOOP_END)].pos != 0 && cols[idx(MarkerType::LOOP_END)].colOnScreen <= x) {
			return;
		}
		if (cols[idx(MarkerType::END)].colOnScreen <= x) {
			return;
		}
	}
	else if (marker_type_ == MarkerType::LOOP_END) {
		if (cols[idx(MarkerType::START)].colOnScreen >= x) {
			return;
		}
		if (cols[idx(MarkerType::LOOP_START)].colOnScreen >= x) {
			return; // Will be a big negative number if inactive
		}
		if (cols[idx(MarkerType::END)].colOnScreen <= x) {
			return;
		}
	}
	else if (marker_type_ == MarkerType::END) {
		if (cols[idx(MarkerType::START)].colOnScreen >= x) {
			return;
		}
		if (cols[idx(MarkerType::LOOP_START)].colOnScreen >= x) {
			return;
		}
		if (cols[idx(MarkerType::LOOP_END)].colOnScreen >= x) {
			return;
		}
	}

	int32_t value = (marker_type_ < MarkerType::LOOP_END) ? start_pos_from_col(nav_, x) : end_pos_from_col(nav_, x);

	{
		const int32_t length = static_cast<int32_t>(nav_.sample->lengthInSamples);
		// Only the END marker, and only in some cases, is allowed to be further right than the
		// waveform length
		if (marker_type_ == MarkerType::END && allow_extra_scroll_right()) {
			if (x > cols[idx(marker_type_)].colOnScreen && value < cols[idx(marker_type_)].pos) {
				return; // Probably not actually necessary
			}
			if (value > length && value < length + static_cast<int32_t>(nav_.xZoom)) {
				value = length;
			}
		}
		else {
			if (value > length) {
				value = length;
			}
		}
	}

	blink_phase_ = 0;
	blink_accum_ms_ = 0;
	write_value(static_cast<uint32_t>(value));
}

Result MarkerScene::horizontal_encoder(int32_t offset, bool zooming, bool shift) {
	Result r;
	r.handled = true;

	// `SampleMarkerEditor::horizontalEncoderAction`'s shift arm (`:688-728`): halve or double the
	// locked loop.
	if (loop_locked() && shift) {
		SampleHolderForVoice& sh = multi_range()->sampleHolder;
		uint32_t proposed_end = sh.loopEndPos;

		if (offset > 0) { // turn clockwise
			const uint32_t loop_length = sh.loopLength();
			proposed_end = sh.loopStartPos + 2 * loop_length;
			if (proposed_end <= sh.endPos) {
				(void)r.actions.push_back(
				    Action{.kind = Action::Kind::Popup, .popup_string = sid(String::STRING_FOR_LOOP_DOUBLED)});
			}
			else {
				(void)r.actions.push_back(
				    Action{.kind = Action::Kind::Popup, .popup_string = sid(String::STRING_FOR_LOOP_TOO_LONG)});
				return r;
			}
		}
		else { // turn anti-clockwise
			const uint32_t loop_length = sh.loopLength();
			if (loop_length > 2) {
				proposed_end = sh.loopStartPos + loop_length / 2;
				(void)r.actions.push_back(
				    Action{.kind = Action::Kind::Popup, .popup_string = sid(String::STRING_FOR_LOOP_HALVED)});
			}
			else {
				(void)r.actions.push_back(
				    Action{.kind = Action::Kind::Popup, .popup_string = sid(String::STRING_FOR_LOOP_TOO_SHORT)});
				return r;
			}
		}

		// temporarily unlock the loop so we can write the end without moving the start
		sh.loopLocked = false;
		write_value(proposed_end, MarkerType::LOOP_END);
		sh.loopLocked = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	MarkerColumn cols[kNumMarkerTypes];
	const MarkerColumn* to_send = nullptr;
	if (marker_type_ != MarkerType::NONE) {
		cols_on_screen(cols);
		to_send = cols;
	}

	bool success = false;
	if (zooming) {
		// `WaveformBasicNavigator::zoom`'s arithmetic only -- see the class docstring on the
		// transition animation.
		success = nav_.zoom(offset, allow_extra_scroll_right(), to_send, marker_type_);
	}
	else {
		success = nav_.scroll(offset, allow_extra_scroll_right(), to_send);
	}

	if (success) {
		record_scroll_and_zoom();
		blink_phase_ = 0;
		blink_accum_ms_ = 0;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}
	return r;
}

Result MarkerScene::handle(const UiEvent& event, const UiContext& ctx) {
	Result r;

	switch (event.kind) {
	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::EncoderRole::Select) {
			// Applied HERE and not parked for `after_render`: `Select` is delivered at
			// `Shell::tick` step 8, strictly AFTER every `after_render()` call, so parking it
			// would cost a whole tick per detent (E3 task 3's finding, in its own words).
			select_encoder(event.delta);
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
			return r;
		}
		if (event.encoder == deluge::ui::EncoderRole::Horizontal) {
			// Legacy zooms while the horizontal encoder's own BUTTON is held
			// (`UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`, set by `buttonAction`'s `X_ENC` arm at
			// `:661-671`); the shell tracks held buttons for us, so the mode is a question rather
			// than a member.
			return horizontal_encoder(event.delta, ctx.held.has(deluge::ui::shell::ButtonId::XEnc), ctx.shift);
		}
		// `verticalEncoderAction` is not ported (see the class docstring); a vertical turn falls
		// through to whatever is beneath.
		return Result::unhandled();

	case UiEvent::Kind::Button:
		if (event.button == deluge::ui::shell::ButtonId::Back && event.on) {
			// `buttonAction`'s BACK arm (`:651-658`) -> `exitUI()` -> `close()`.
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kMarkerSceneKind});
			return r;
		}
		return Result::unhandled();

	case UiEvent::Kind::Pad: {
		const int32_t x = event.x;
		if (x == kDisplayWidth) {
			// The mute column: legacy exits on a press here (`:361-365`).
			if (event.on) {
				r.handled = true;
				(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kMarkerSceneKind});
			}
			else {
				r.handled = true;
			}
			return r;
		}
		if (x > kDisplayWidth) {
			// The audition column, which legacy passes down to `instrumentClipView` (`:353-358`)
			// and returns DEALT_WITH for. This layer may not name a root UI, so the press is
			// swallowed rather than delegated -- auditioning from inside the marker editor is the
			// one input this port drops outright.
			r.handled = true;
			return r;
		}
		pad_action(x, event.y, event.on);
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	case UiEvent::Kind::EncoderButton:
	case UiEvent::Kind::AllReleased:
		return Result::unhandled();
	}
	return Result::unhandled();
}

// ---------------------------------------------------------------------------------------------
// Time

void MarkerScene::tick(uint32_t dt_ms) {
	if (!marker_on_screen_) {
		// `timerCallback`'s early return (`:797-800`): no marker visible, no blink.
		return;
	}
	blink_accum_ms_ += dt_ms;
	while (blink_accum_ms_ >= kBlinkPhaseMs) {
		blink_accum_ms_ -= kBlinkPhaseMs;
		blink_phase_ = static_cast<int8_t>((blink_phase_ + 1) % 4);
	}
}

std::optional<uint32_t> MarkerScene::next_timer() const {
	if (!marker_on_screen_) {
		return std::nullopt;
	}
	return kBlinkPhaseMs - std::min(blink_accum_ms_, kBlinkPhaseMs);
}

// ---------------------------------------------------------------------------------------------
// Rendering

void MarkerScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	(void)role;
	if (nav_.sample == nullptr) {
		return;
	}

	MarkerColumn cols[kNumMarkerTypes];
	cols_on_screen(cols);
	const uint32_t pos = static_cast<uint32_t>(cols[idx(marker_type_)].pos);

	// `renderOLED`'s title switch (`:1107-1126`), through `reverseRemap` -- so a reversed sample's
	// START marker is labelled "End point".
	std::string_view label;
	switch (reverse_remap(marker_type_)) {
	case MarkerType::START:
		label = "Start point";
		break;
	case MarkerType::END:
		label = "End point";
		break;
	case MarkerType::LOOP_START:
		label = "Loop start";
		break;
	case MarkerType::LOOP_END:
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
	deluge::ui::oled::title(f, label);

	if (loop_locked()) {
		deluge::ui::oled::title_badge(f, deluge::gfx::oled::icons::markerLock);
	}

	deluge::ui::oled::sample_position(
	    f, deluge::gfx::oled::Rect{1, body_y(f), f.bounds().w - 1, 2 * deluge::ui::oled::kSamplePositionLineGap}, pos,
	    nav_.sample->sampleRate);
}

void MarkerScene::render_grid(deluge::ui::grid::GridFrame& f) {
	if (nav_.sample == nullptr) {
		return;
	}

	const int32_t main_cols = std::min<int32_t>(f.bounds().w, kDisplayWidth);

	// `renderMainPads`' `waveformRenderer.renderFullScreen` (`:1369`), split into its two halves:
	// the scan (model) and the paint (toolkit).
	//
	// The scan can read the card, exactly as legacy's does from inside `renderMainPads`. A column
	// it could not load stays `COL_STATUS_NOT_INVESTIGATED` and paints black, which is
	// `waveform_full`'s own fallback -- the return value is not consulted for that reason.
	(void)deluge::sample::find_peaks_per_col(nav_.sample, nav_.xScroll, static_cast<uint64_t>(nav_.xZoom),
	                                         &nav_.renderData);

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

	// `renderOneCol`'s monochrome arm: `rgb` is `nullopt` on this path, so `brightness` is 128
	// (`waveform_renderer.cpp:244`). `waveform_full`'s precondition, honoured.

	f.region(deluge::gfx::grid::Rect{0, 0, main_cols, f.bounds().h}, [&](deluge::ui::grid::GridFrame& main) {
		deluge::ui::grid::waveform_full(main, peaks, deluge::ui::grid::kWaveformBarBrightnessMono, std::nullopt);

		if (marker_type_ == MarkerType::NONE) {
			return;
		}

		MarkerColumn cols[kNumMarkerTypes];
		cols_on_screen(cols);

		// The blink, as `timerCallback` (`:806-862`) draws it rather than as `renderMainPads`
		// (`:1376-1387`) does -- see the class docstring for why the two disagree.
		uint32_t suppress = 0;
		if (blink_phase_ == 3) {
			suppress |= 1u << idx(marker_type_);
			if (loop_locked()) {
				if (marker_type_ == MarkerType::LOOP_START) {
					suppress |= 1u << idx(MarkerType::LOOP_END);
				}
				if (marker_type_ == MarkerType::LOOP_END) {
					suppress |= 1u << idx(MarkerType::LOOP_START);
				}
			}
		}

		const int32_t selected_col = cols[idx(marker_type_)].colOnScreen;

		// Colours are resolved here rather than in the widget: `reverseRemap` reads
		// `SampleControls`, which `src/ui` may not include.
		std::array<RGB, kNumMarkerTypes> colours{};
		for (int32_t m = 0; m < kNumMarkerTypes; m++) {
			colours[static_cast<size_t>(m)] =
			    kMarkerColours[static_cast<size_t>(idx(reverse_remap(static_cast<MarkerType>(m))))];
		}

		for (int32_t col = 0; col < main_cols; col++) {
			if (col == selected_col && blink_phase_ == 1) {
				// Phase 1: the whole column flashes the selected marker's colour (`:815`).
				deluge::ui::grid::marker_tint(main, col, 0, main.bounds().h,
				                              colours[static_cast<size_t>(idx(marker_type_))], false);
				continue;
			}
			uint32_t active = 0;
			for (int32_t m = 0; m < kNumMarkerTypes; m++) {
				if (cols[m].colOnScreen == col) {
					active |= 1u << m;
				}
			}
			if (active == 0) {
				continue;
			}
			deluge::ui::grid::marker_column(
			    main, col,
			    deluge::ui::grid::MarkerStack{.active = active, .suppress = suppress, .selected = idx(marker_type_)},
			    colours);
		}

		// `renderMainPads`' timer re-arm (`:1400-1403`): the blink only runs while the selected
		// marker is actually inside the display.
		marker_on_screen_ = selected_col >= 0 && selected_col < main_cols;
		if (marker_on_screen_) {
			// This arm is what *used* to make the blink visible at all: `tick()` advancing
			// `blink_phase_` changes nothing anyone can see unless the surface is asked for another
			// frame, and until E3 task 10 `next_timer()` only woke the shell without marking the
			// pads dirty (`RepaintGate::needs_frame`). Found by writing the blink's render example
			// and watching all four phases come back byte-identical. `Shell::tick`'s step 8b now
			// arms this same gate from `next_timer()` itself, so the blink no longer depends on
			// this line -- it is kept as the tighter statement (armed from the frame being drawn,
			// and only while the marker is on screen). Legacy has no equivalent either way, because
			// its `timerCallback` paints straight into `PadLEDs::image` itself.
			f.gate().request_repaint_after(kBlinkPhaseMs, f.now_ms());
		}
	});
}

MarkerScene& marker_scene() {
	static MarkerScene scene;
	return scene;
}

} // namespace deluge::scenes::marker
