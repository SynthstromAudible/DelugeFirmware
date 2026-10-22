/// @file
/// @brief `MarkerScene`. See `marker_scene.h`.

#include "scenes/marker/marker_scene.h"

#include "ui/core/metrics.h"
#include "ui/grid/frame.h"
#include "ui/oled/sample_position.h"
#include "ui/oled/text.h"

#include "gfx/grid/palette.h"
#include "gfx/oled/icons.h"

#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/services/view_state.h"
#include "scenes/services/waveforms.h"

#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "gui/l10n/strings.h"
#include "model/sample/waveform_scan.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

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

std::optional<services::WaveformAddress> MarkerScene::address_of(const menu::MenuContext& ctx) {
	// `getCurrentSampleHolder` (`sample_marker_editor.cpp:62`): the audio clip's own sample, else the
	// selected multisample range's.
	if (ctx.is_audio_clip) {
		if (!ctx.addresses.clip) {
			return std::nullopt;
		}
		return services::WaveformAddress{.kind = services::WaveformAddress::Kind::AUDIO_CLIP,
		                                 .clip = *ctx.addresses.clip};
	}
	if (!ctx.addresses.slot || ctx.range_index < 0) {
		return std::nullopt;
	}
	return services::WaveformAddress{.kind = services::WaveformAddress::Kind::RANGE,
	                                 .slot = *ctx.addresses.slot,
	                                 .source = static_cast<uint8_t>(ctx.source_index),
	                                 .range = static_cast<uint16_t>(ctx.range_index)};
}

const deluge::instrument::SourceState* MarkerScene::source_state() const {
	if (ctx_.is_audio_clip || ctx_.facts == nullptr) {
		return nullptr;
	}
	const deluge::instrument::SoundState* state = ctx_.facts->sound_state(where_.slot);
	return state != nullptr && where_.source < state->sources.size() ? &state->sources[where_.source] : nullptr;
}

const deluge::instrument::RangeState* MarkerScene::range_state() const {
	const deluge::instrument::SourceState* source = source_state();
	return source != nullptr && where_.range < source->ranges.size() ? &source->ranges[where_.range] : nullptr;
}

int64_t MarkerScene::audio_clip_row(std::string_view path) const {
	if (ctx_.model == nullptr) {
		return 0;
	}
	const std::optional<deluge::serialization::Value> value = deluge::project::read_setting(
	    *ctx_.model, {.group = deluge::project::SettingGroupId::AUDIO_CLIP, .owner = where_.clip}, path);
	return value ? deluge::serialization::int_of(&*value).value_or(0) : 0;
}

MarkerScene::Zone MarkerScene::zone() const {
	if (ctx_.is_audio_clip) {
		// An audio clip has no loop markers at all (`:255-261`).
		return {.start = static_cast<uint64_t>(audio_clip_row("startSamplePos")),
		        .end = static_cast<uint64_t>(audio_clip_row("endSamplePos"))};
	}
	const deluge::instrument::RangeState* range = range_state();
	if (range == nullptr) {
		return {};
	}
	return {.start = range->start_pos,
	        .end = range->end_pos,
	        .loop_start = range->loop_start_pos,
	        .loop_end = range->loop_end_pos};
}

bool MarkerScene::reversed() const {
	if (ctx_.is_audio_clip) {
		return audio_clip_row("reversed") != 0;
	}
	const deluge::instrument::SourceState* source = source_state();
	return source != nullptr && source->reversed;
}

std::string MarkerScene::file() const {
	const deluge::instrument::RangeState* range = range_state();
	return range != nullptr ? range->file_path : std::string{};
}

services::SampleView MarkerScene::view() const {
	// A view remembered for another file -- the range now holds a different sample -- is not this one's.
	services::SampleView v = services::view_state().sample_view(where_, file()).value_or(services::SampleView{});
	v.file = file();
	return v;
}

void MarkerScene::follow_waveform(const services::WaveformInfo& info) {
	nav_.length_samples = info.length_samples;
	if (info.identity != waveform_identity_) {
		waveform_identity_ = info.identity;
		nav_.renderData.xScroll = -1; // the cached columns are another waveform's
	}
}

bool MarkerScene::loop_locked() const {
	// `isLoopLocked()` (`:79`): only a sound's range has a loop to lock.
	return !ctx_.is_audio_clip && view().loop_locked;
}

MarkerType MarkerScene::reverse_remap(MarkerType type) const {
	// `SampleMarkerEditor::reverseRemap` (`:83`).
	if (!reversed()) {
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
	// `LoopPoint::checkPermissionToBeginSession` before it ever opens: no file loaded where the
	// context points, or nowhere to point.
	const std::optional<services::WaveformAddress> address = address_of(ctx);
	return address && menu::host_waveforms().info(*address).has_value();
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
	// `sampleMarkerEditor.markerType` (`sample/loop_point.cpp:69`) immediately before `openUI`
	// (`:71`; and `audio_clip/sample_marker_editor.cpp` does the same); here it travels with the id instead of through
	// a member the caller has to remember to write.
	const auto requested = static_cast<MarkerType>(static_cast<int16_t>(id.arg));
	marker_type_ = (requested == MarkerType::START || requested == MarkerType::LOOP_START
	                || requested == MarkerType::LOOP_END || requested == MarkerType::END)
	                   ? requested
	                   : MarkerType::START;

	where_ = *address_of(ctx_);
	services::view_state().forget_unless(ctx_.model != nullptr ? ctx_.model->identity() : 0);
	nav_.sample = nullptr;
	waveform_identity_ = 0;
	follow_waveform(*menu::host_waveforms().info(where_));
	const services::SampleView remembered = view();
	nav_.opened(remembered.scroll, remembered.zoom); // `:126`

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
	const Zone z = zone();
	const auto start_col = [this](int32_t pos) {
		return divide_round_negative(static_cast<int32_t>(pos - nav_.xScroll), static_cast<int32_t>(nav_.xZoom));
	};
	const auto end_col = [this](int32_t pos) {
		return divide_round_negative(static_cast<int32_t>(pos - 1 - nav_.xScroll), static_cast<int32_t>(nav_.xZoom));
	};

	cols[idx(MarkerType::START)].pos = static_cast<int32_t>(z.start);
	cols[idx(MarkerType::START)].colOnScreen = start_col(cols[idx(MarkerType::START)].pos);

	if (!ctx_.is_audio_clip) {
		cols[idx(MarkerType::LOOP_START)].pos = static_cast<int32_t>(z.loop_start);
		cols[idx(MarkerType::LOOP_START)].colOnScreen = cols[idx(MarkerType::LOOP_START)].pos != 0
		                                                    ? start_col(cols[idx(MarkerType::LOOP_START)].pos)
		                                                    : kInvalidColumn;

		cols[idx(MarkerType::LOOP_END)].pos = static_cast<int32_t>(z.loop_end);
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

	cols[idx(MarkerType::END)].pos = static_cast<int32_t>(z.end);
	cols[idx(MarkerType::END)].colOnScreen = end_col(cols[idx(MarkerType::END)].pos);
}

int32_t start_pos_from_col(const deluge::sample::WaveformNav& nav, int32_t col) {
	return static_cast<int32_t>(nav.xScroll + col * nav.xZoom); // `:228`
}

int32_t end_pos_from_col(const deluge::sample::WaveformNav& nav, int32_t col) {
	return static_cast<int32_t>(nav.xScroll + (col + 1) * nav.xZoom); // `:232`
}

void MarkerScene::record_scroll_and_zoom() {
	// `SampleMarkerEditor::recordScrollAndZoom` (`:140`), into the view-state store.
	if (marker_type_ != MarkerType::NONE) {
		services::SampleView v = view();
		v.scroll = nav_.xScroll;
		v.zoom = nav_.xZoom;
		services::view_state().set_sample_view(where_, v);
	}
}

bool MarkerScene::allow_extra_scroll_right() const {
	// `SampleMarkerEditor::shouldAllowExtraScrollRight` (`:1044`).
	if (marker_type_ == MarkerType::NONE || reversed()) {
		return false;
	}
	if (ctx_.is_audio_clip) {
		return true;
	}
	const deluge::instrument::SourceState* source = source_state();
	return source != nullptr && source->repeat_mode == SampleRepeatMode::STRETCH;
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
	if (ctx_.is_audio_clip) {
		return;
	}
	services::SampleView v = view();
	v.loop_locked = locked;
	services::view_state().set_sample_view(where_, v);
}

// ---------------------------------------------------------------------------------------------
// Writing

void MarkerScene::write_value(uint32_t value, MarkerType which) {
	// `SampleMarkerEditor::writeValue` (`:148`).
	if (which == MarkerType::NOT_AVAILABLE) {
		which = marker_type_;
	}

	if (ctx_.sink == nullptr) {
		return;
	}
	if (ctx_.is_audio_clip) {
		// An audio clip's markers are rows of its record; the app moves one with the clip's voice taken off the
		// sample, the moved part's clusters claimed and a playing clip resumed.
		if (which == MarkerType::START || which == MarkerType::END) {
			(void)ctx_.sink->move_audio_clip_marker(where_.clip,
			                                        which == MarkerType::START ? "startSamplePos" : "endSamplePos",
			                                        static_cast<int64_t>(value));
		}
		return;
	}

	// A sound's markers are its range's zone: the new zone is built from the source's ranges and written as one
	// request, which the app applies to the record and the clusters a moved zone needs, then lets a sounding voice
	// follow the moved marker (`:203-215`).
	const deluge::instrument::SourceState* source = source_state();
	if (source == nullptr || where_.range >= source->ranges.size()) {
		return;
	}
	std::vector<deluge::instrument::RangeState> ranges = source->ranges;
	deluge::instrument::RangeState& range = ranges[where_.range];
	const bool locked = loop_locked();
	const uint32_t loop_length = range.loop_end_pos - range.loop_start_pos;
	if (which == MarkerType::START) {
		range.start_pos = value;
	}
	else if (which == MarkerType::LOOP_START) {
		if (locked) {
			const uint32_t intended_end = value + loop_length;
			if (uint64_t{intended_end} <= range.end_pos) {
				range.loop_start_pos = value;
				range.loop_end_pos = intended_end;
			}
		}
		else {
			range.loop_start_pos = value;
		}
	}
	else if (which == MarkerType::LOOP_END) {
		if (locked) {
			const int32_t intended_start = static_cast<int32_t>(value - loop_length);
			// pos == 0 would disable the loop, so the smallest legal start position is sample 1
			if (intended_start >= 1 && static_cast<uint64_t>(intended_start) >= range.start_pos) {
				range.loop_end_pos = value;
				range.loop_start_pos = intended_start;
			}
		}
		else {
			range.loop_end_pos = value;
		}
	}
	else if (which == MarkerType::END) {
		range.end_pos = value;
	}
	(void)ctx_.sink->write_ranges(where_.slot, where_.source, ranges,
	                              {.kind = bind::RangesCause::Kind::MARKER, .marker = static_cast<int8_t>(which)});
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
			if (new_pos > static_cast<int32_t>(nav_.length())) {
				new_pos = static_cast<int32_t>(nav_.length());
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
			if (cols[idx(MarkerType::LOOP_END)].pos != 0 && cols[idx(MarkerType::LOOP_END)].colOnScreen <= x) {
				return;
			}
			if (cols[idx(MarkerType::END)].colOnScreen <= x) {
				return;
			}
			const int32_t new_value = start_pos_from_col(nav_, x);
			// Loop start and end points are not allowed to be further right than the waveform length
			if (new_value >= static_cast<int32_t>(nav_.length())) {
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
			if (new_value >= static_cast<int32_t>(nav_.length())) {
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
			set_loop_locked(!loop_locked());
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
		const int32_t length = static_cast<int32_t>(nav_.length());
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
		const Zone z = zone();
		uint32_t proposed_end = z.loop_end;
		const uint32_t loop_length = z.loop_end - z.loop_start;

		if (offset > 0) { // turn clockwise
			proposed_end = z.loop_start + 2 * loop_length;
			if (proposed_end <= z.end) {
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
			if (loop_length > 2) {
				proposed_end = z.loop_start + loop_length / 2;
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
		set_loop_locked(false);
		write_value(proposed_end, MarkerType::LOOP_END);
		set_loop_locked(true);
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

	// The two MIDI kinds (F1 task 10's transport, `shell/scene.h`) are declined here for the reason
	// every scene ported so far declines them: claiming one means answering `learnable_at(pad)`,
	// which is the timeline scenes' work (E5/E6). Declining lets `Router::run_chain` carry the
	// message on to `services::MidiLearn`, which is where legacy's `view.` fallback puts it.
	case UiEvent::Kind::MidiNoteOn:
	case UiEvent::Kind::MidiCc:
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
	const std::optional<services::WaveformInfo> info = menu::host_waveforms().info(where_);
	if (!info) {
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
	    info->sample_rate);
}

void MarkerScene::render_grid(deluge::ui::grid::GridFrame& f) {
	const std::optional<services::WaveformInfo> info = menu::host_waveforms().info(where_);
	if (!info) {
		return;
	}
	follow_waveform(*info);

	const int32_t main_cols = std::min<int32_t>(f.bounds().w, kDisplayWidth);

	// `renderMainPads`' `waveformRenderer.renderFullScreen` (`:1369`), split into its two halves:
	// the scan (model) and the paint (toolkit).
	//
	// The scan can read the card, exactly as legacy's does from inside `renderMainPads`. A column
	// it could not load stays `COL_STATUS_NOT_INVESTIGATED` and paints black, which is
	// `waveform_full`'s own fallback -- the return value is not consulted for that reason.
	(void)menu::host_waveforms().peaks(where_, nav_.xScroll, static_cast<uint64_t>(nav_.xZoom), nav_.renderData);

	std::array<deluge::ui::grid::PeakCol, kDisplayWidth> peaks{};
	const int32_t centre = info->centre;
	const int32_t span = info->span;
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
