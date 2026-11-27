/// @file
/// @brief `MarkerScene`. See `marker_scene.h`.

#include "ui/scenes/marker/marker_scene.h"

#include "instruments/classic/words.h"
#include "project/sample_marker.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/grid/columns.h"
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/oled/sample_position.h"
#include "ui/widgets/oled/text.h"

#include "ui/gfx/grid/pad_grid.h"
#include "ui/gfx/grid/palette.h"
#include "ui/gfx/grid/tween.h"
#include "ui/gfx/oled/icons.h"

#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/bind/ports/sound_writes.h"
#include "ui/scenes/bind/ports/waveforms.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/view_state.h"
#include "ui/scenes/services/waveform_paint.h"

#include "base/divide.h"
#include "ui/views/waveform/view.h"

#include "gui/l10n/strings.h"
#include "instruments/classic/state_access.h"
#include "project/device_settings.h"
#include "project/setting_group.h"
#include "project/song.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace deluge::ui::scenes::marker {

/// @brief The pad grid's main columns and rows.
constexpr int32_t kMainCols = deluge::ui::gfx::grid::kDelugeGrid.main_cols;
constexpr int32_t kRows = deluge::ui::gfx::grid::kDelugeGrid.rows;

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

/// @brief A pin's column for the comparisons that place and refuse markers: an unset marker compares as left of every
///        column, so every `column <= x` test refuses on it and every `column >= x` test passes it by.
[[nodiscard]] constexpr int64_t column_or_far_left(const views::waveform::MarkerPin& p) {
	return p.column.value_or(std::numeric_limits<int64_t>::min());
}

/// @brief The last frame a range's loop point can name: its record (`synth::SourceRange`'s zone) and the preset and
/// song
///        files store loop points as 32-bit unsigned.
constexpr int64_t kLastLoopPoint = std::numeric_limits<uint32_t>::max();

/// @brief The last frame an audio clip's start or end can name: the song file and the clip's rows store them as 32-bit
///        signed.
constexpr int64_t kLastAudioClipMarker = std::numeric_limits<int32_t>::max();

using deluge::project::kSampleMarkerCount;
using deluge::project::SampleMarker;

int32_t idx(SampleMarker t) {
	return std::to_underlying(t);
}

/// @brief The marker at position @p m along the sample, 0 to 3.
SampleMarker marker_at(int32_t m) {
	return static_cast<SampleMarker>(m);
}

/// `SampleMarkerEditor::renderMarkerInCol`'s switch (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`), in
/// `SampleMarker` order.
const std::array<RGB, kSampleMarkerCount> kMarkerColours{
    deluge::gui::colours::green,
    deluge::gui::colours::cyan,
    deluge::gui::colours::magenta,
    deluge::gui::colours::red,
};

/// The body's first line: legacy's `OLED_MAIN_TOPMOST_PIXEL + 17`. The frame's origin
/// already accounts for the bezel, so this is `title_height + 5` -- and reading it off the style
/// rather than hard-coding 17 is what keeps the two lines under the separator on a panel whose
/// title face is taller.
int32_t body_y(const deluge::ui::widgets::oled::OledFrame& f) {
	return f.style.title_height + 5;
}

} // namespace

std::span<const RGB> marker_colours() {
	return std::span<const RGB>{kMarkerColours};
}

// ---------------------------------------------------------------------------------------------
// Context

std::optional<bind::WaveformAddress> MarkerScene::address_of(const menu::MenuContext& ctx) {
	// `getCurrentSampleHolder` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`): the audio clip's own sample,
	// else the selected multisample range's.
	if (ctx.is_audio_clip) {
		if (!ctx.addresses.clip) {
			return std::nullopt;
		}
		return bind::WaveformAddress{.kind = bind::WaveformAddress::Kind::AUDIO_CLIP, .clip = *ctx.addresses.clip};
	}
	if (!ctx.addresses.slot || ctx.range_index < 0) {
		return std::nullopt;
	}
	return bind::WaveformAddress{.kind = bind::WaveformAddress::Kind::RANGE,
	                             .slot = *ctx.addresses.slot,
	                             .source = static_cast<uint8_t>(ctx.source_index),
	                             .range = static_cast<uint16_t>(ctx.range_index)};
}

const deluge::instruments::classic::SoundState* MarkerScene::sound_state() const {
	if (ctx_.is_audio_clip || ctx_.ports.sound == nullptr || where_.source >= instruments::classic::kSourceCount) {
		return nullptr;
	}
	return ctx_.ports.sound->sound_state(where_.slot);
}

deluge::instruments::classic::SourceId MarkerScene::source_id() const {
	return static_cast<deluge::instruments::classic::SourceId>(where_.source);
}

std::optional<synth::SourceRange> MarkerScene::edited_range() const {
	const deluge::instruments::classic::SoundState* sound = sound_state();
	if (sound == nullptr) {
		return std::nullopt;
	}
	const std::vector<synth::SourceRange>& ranges = deluge::instruments::classic::source_ranges(*sound, source_id());
	return where_.range < ranges.size() ? std::optional{ranges[where_.range]} : std::nullopt;
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
		// An audio clip has no loop markers at all.
		return {.start = static_cast<uint64_t>(audio_clip_row("startSamplePos")),
		        .end = static_cast<uint64_t>(audio_clip_row("endSamplePos"))};
	}
	const std::optional<synth::SourceRange> range = edited_range();
	if (!range) {
		return {};
	}
	return {.start = range->zone.start,
	        .end = range->zone.end,
	        .loop_start = range->zone.loop_start,
	        .loop_end = range->zone.loop_end};
}

bool MarkerScene::reversed() const {
	if (ctx_.is_audio_clip) {
		return audio_clip_row("reversed") != 0;
	}
	const deluge::instruments::classic::SoundState* sound = sound_state();
	return sound != nullptr && deluge::instruments::classic::sample_playback(*sound, source_id()).reversed;
}

std::optional<services::ViewKey> MarkerScene::view_key_of() const {
	// Main keeps the view and the lock on the sample holder: the audio clip's own, or the range's. The range's
	// identity names that range object, so its view stays with it whatever file is loaded into it and wherever it
	// moves among its source's ranges.
	if (where_.kind == bind::WaveformAddress::Kind::AUDIO_CLIP) {
		return services::ViewKey{.kind = services::ViewKey::Kind::AUDIO_CLIP, .id = where_.clip.clip_id};
	}
	const std::optional<uint64_t> range =
	    ctx_.ports.sound != nullptr ? ctx_.ports.sound->range_identity(where_.slot, where_.source, where_.range)
	                                : std::nullopt;
	if (!range) {
		return std::nullopt;
	}
	return services::ViewKey{.kind = services::ViewKey::Kind::RANGE, .id = *range};
}

services::SampleView MarkerScene::view() const {
	return view_key_ ? services::view_state().sample_view(*view_key_).value_or(services::SampleView{})
	                 : services::SampleView{};
}

void MarkerScene::follow_waveform(const bind::WaveformInfo& info) {
	if (info.identity != identity_) {
		identity_ = info.identity;
		view_ = views::waveform::WaveformView::open(info.length_samples, view_.window());
		retry_.restart();
		return;
	}
	view_ = view_.with_length(info.length_samples);
}

bool MarkerScene::loop_locked() const {
	// `isLoopLocked()`: only a sound's range has a loop to lock.
	return !ctx_.is_audio_clip && view().loop_locked;
}

SampleMarker MarkerScene::reverse_remap(SampleMarker type) const {
	// `SampleMarkerEditor::reverseRemap`.
	return reversed() ? mirrored(type) : type;
}

bool MarkerScene::can(const menu::MenuContext& ctx) {
	// `SampleMarkerEditor::opened`'s refusal, plus the holder test legacy gets from
	// `LoopPoint::checkPermissionToBeginSession` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`) before
	// it ever opens: no file loaded where the context points, or nowhere to point.
	const std::optional<bind::WaveformAddress> address = address_of(ctx);
	return address && ctx.ports.waveforms != nullptr && ctx.ports.waveforms->info(*address).has_value();
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

	// `SceneId::arg` is the `SampleMarker` to start on. Legacy's two openers set `sampleMarkerEditor.markerType`
	// immediately before `openUI`: `LoopPoint::beginSession` in
	// `origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp` and the opener in
	// `origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`; here it travels with the id instead
	// of through a member the caller has to remember to write.
	const auto requested = static_cast<int16_t>(id.arg);
	marker_type_ = requested >= 0 && requested < static_cast<int16_t>(kSampleMarkerCount) ? marker_at(requested)
	                                                                                      : SampleMarker::START;

	where_ = *address_of(ctx_);
	view_key_ = view_key_of();
	services::view_state().forget_unless(ctx_.model != nullptr ? ctx_.model->identity() : 0);
	const std::optional<bind::WaveformInfo> info = waveform_info();
	const services::SampleView remembered = view();
	view_ = views::waveform::WaveformView::open(
	    info->length_samples, remembered.zoom != 0 ? std::optional{views::waveform::Window{.scroll = remembered.scroll,
	                                                                                       .zoom = remembered.zoom}}
	                                               : std::nullopt);
	identity_ = info->identity;
	retry_.restart();

	blink_phase_ = 0;
	blink_accum_ms_ = 0;
	press_x_ = -1;
	press_y_ = -1;
	marker_on_screen_ = false;
	forget_modes();
	return true;
}

void MarkerScene::on_exit() {
	// Legacy has no `close()` hook of its own -- `exitUI` just calls `close()`. The one
	// thing worth dropping is the held-marker gesture, which must not survive into the next entry:
	// `currentUIMode` is a global legacy clears on the way out of every UI, and this scene's copy
	// of it is a member.
	press_x_ = -1;
	press_y_ = -1;
	forget_modes();
}

void MarkerScene::forget_modes() {
	holding_ = false;
	audition_under_hold_ = false;
	xenc_held_ = false;
	zoom_pending_.reset();
	zoom_.active = false;
	shown_.reset();
	playing_ = false;
	last_ticks_.fill(deluge::ui::widgets::grid::kNoTickSquare);
}

// ---------------------------------------------------------------------------------------------
// Marker positions

// UI-tier code, run per input and per frame, never by the audio tier: it runs from SDRAM, as the GUI's code does.
std::array<views::waveform::MarkerPin, kSampleMarkerCount> MarkerScene::pins() const {
	// `SampleMarkerEditor::getColsOnScreen` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`), with its two
	// column mappings inlined: `getStartColOnScreen` floors `(pos - xScroll) / xZoom`, and `getEndColOnScreen` floors
	// `(pos - 1 - xScroll) / xZoom` -- an end marker names the column its LAST sample is in, which is why the two are
	// not the same function.
	const Zone z = zone();
	const auto start_pin = [this](int64_t pos) {
		return views::waveform::MarkerPin{
		    .position = pos, .column = base::floor_div(pos - view_.scroll(), view_.zoom()), .right_edge = false};
	};
	const auto end_pin = [this](int64_t pos) {
		return views::waveform::MarkerPin{
		    .position = pos, .column = base::floor_div(pos - 1 - view_.scroll(), view_.zoom()), .right_edge = true};
	};
	const auto unset = [](bool right_edge) {
		return views::waveform::MarkerPin{.position = 0, .column = std::nullopt, .right_edge = right_edge};
	};

	std::array<views::waveform::MarkerPin, kSampleMarkerCount> pins{};
	pins[idx(SampleMarker::START)] = start_pin(static_cast<int64_t>(z.start));
	// An audio clip has no loop markers at all; a range's loop point at 0 is not set.
	pins[idx(SampleMarker::LOOP_START)] =
	    !ctx_.is_audio_clip && z.loop_start != 0 ? start_pin(z.loop_start) : unset(false);
	pins[idx(SampleMarker::LOOP_END)] = !ctx_.is_audio_clip && z.loop_end != 0 ? end_pin(z.loop_end) : unset(true);
	pins[idx(SampleMarker::END)] = end_pin(static_cast<int64_t>(z.end));
	return pins;
}

// `SampleMarkerEditor::getStartPosFromCol` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`).
int64_t start_pos_from_col(const views::waveform::WaveformView& view, int64_t col) {
	return view.scroll() + col * view.zoom();
}

// `SampleMarkerEditor::getEndPosFromCol` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`).
int64_t end_pos_from_col(const views::waveform::WaveformView& view, int64_t col) {
	return view.scroll() + (col + 1) * view.zoom();
}

void MarkerScene::record_scroll_and_zoom() {
	// `SampleMarkerEditor::recordScrollAndZoom` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`), into the
	// view-state store.
	if (view_key_) {
		services::SampleView v = view();
		v.scroll = view_.scroll();
		v.zoom = view_.zoom();
		services::view_state().set_sample_view(*view_key_, v);
	}
}

bool MarkerScene::allow_extra_scroll_right() const {
	// `SampleMarkerEditor::shouldAllowExtraScrollRight`.
	if (reversed()) {
		return false;
	}
	if (ctx_.is_audio_clip) {
		return true;
	}
	const deluge::instruments::classic::SoundState* sound = sound_state();
	return sound != nullptr
	       && deluge::instruments::classic::sample_playback(*sound, source_id()).repeat_mode
	              == SampleRepeatMode::STRETCH;
}

void MarkerScene::set_loop_locked(bool locked, bool announce) {
	// `loopLock` / `loopUnlock` (`displayPopup("LOCK")` / `("FREE")`).
	if (ctx_.is_audio_clip) {
		return;
	}
	if (view_key_) {
		services::SampleView v = view();
		v.loop_locked = locked;
		services::view_state().set_sample_view(*view_key_, v);
	}
	if (announce) {
		pending_popup_ = locked ? String::STRING_FOR_LOOP_LOCKED : String::STRING_FOR_LOOP_FREE;
	}
}

// ---------------------------------------------------------------------------------------------
// Writing

void MarkerScene::write_value(int64_t value, std::optional<SampleMarker> marker) {
	// `SampleMarkerEditor::writeValue`.
	const SampleMarker which = marker.value_or(marker_type_);

	if (ctx_.is_audio_clip) {
		// An audio clip's markers are rows of its record; the app moves one with the clip's voice taken off the
		// sample, the moved part's clusters claimed and a playing clip resumed.
		if (ctx_.ports.clip_writes != nullptr && (which == SampleMarker::START || which == SampleMarker::END)) {
			(void)ctx_.ports.clip_writes->move_audio_clip_marker(
			    where_.clip, which == SampleMarker::START ? "startSamplePos" : "endSamplePos",
			    std::min(value, kLastAudioClipMarker));
		}
		return;
	}
	if (ctx_.ports.sound_writes == nullptr) {
		return;
	}

	// A sound's markers are its range's zone: the new zone is built from the source's ranges and written as one
	// request, which the app applies to the record and the clusters a moved zone needs, then lets a sounding voice
	// follow the moved marker.
	const deluge::instruments::classic::SoundState* sound = sound_state();
	if (sound == nullptr) {
		return;
	}
	const std::vector<synth::SourceRange> held = deluge::instruments::classic::source_ranges(*sound, source_id());
	if (where_.range >= held.size()) {
		return;
	}
	std::vector<synth::SourceRange> ranges{held.begin(), held.end()};
	synth::SourceRange& range = ranges[where_.range];
	const bool locked = loop_locked();
	const int64_t loop_length = int64_t{range.zone.loop_end} - range.zone.loop_start;
	const auto loop_point = [](int64_t v) { return static_cast<uint32_t>(std::min(v, kLastLoopPoint)); };
	if (which == SampleMarker::START) {
		range.zone.start = static_cast<uint64_t>(value);
	}
	else if (which == SampleMarker::LOOP_START) {
		if (locked) {
			const int64_t intended_end = value + loop_length;
			if (intended_end <= static_cast<int64_t>(range.zone.end) && intended_end <= kLastLoopPoint) {
				range.zone.loop_start = loop_point(value);
				range.zone.loop_end = loop_point(intended_end);
			}
		}
		else {
			range.zone.loop_start = loop_point(value);
		}
	}
	else if (which == SampleMarker::LOOP_END) {
		if (locked) {
			const int64_t intended_start = value - loop_length;
			// pos == 0 would disable the loop, so the smallest legal start position is sample 1
			if (intended_start >= 1 && intended_start >= static_cast<int64_t>(range.zone.start)
			    && value <= kLastLoopPoint) {
				range.zone.loop_end = loop_point(value);
				range.zone.loop_start = loop_point(intended_start);
			}
		}
		else {
			range.zone.loop_end = loop_point(value);
		}
	}
	else if (which == SampleMarker::END) {
		range.zone.end = static_cast<uint64_t>(value);
	}
	(void)ctx_.ports.sound_writes->write_ranges(where_.slot, where_.source, ranges,
	                                            {.kind = bind::RangesCause::Kind::MARKER, .marker = which});
}

// ---------------------------------------------------------------------------------------------
// Input

void MarkerScene::select_encoder(int32_t offset) {
	// `SampleMarkerEditor::selectEncoderAction`: refused under any mode but auditioning.
	if (holding_ || xenc_held_ || sliding()) {
		return;
	}
	std::array<views::waveform::MarkerPin, kSampleMarkerCount> cols = pins();

	const int64_t old_col = column_or_far_left(cols[idx(marker_type_)]);
	const int64_t old_pos = cols[idx(marker_type_)].position;
	// An unset marker has no column to move from: it stays far left, and its write below is 0, which leaves it unset.
	const bool set = cols[idx(marker_type_)].column.has_value();
	int64_t new_col = set ? old_col + offset : old_col;

	// Make sure we don't drive one marker into the other
	for (int32_t c = 0; c < static_cast<int32_t>(kSampleMarkerCount); c++) {
		if (c == idx(marker_type_)) {
			continue;
		}
		if (column_or_far_left(cols[c]) == old_col || column_or_far_left(cols[c]) == new_col) {
			return;
		}
	}

	int64_t new_pos = !set                                      ? 0
	                  : (marker_type_ < SampleMarker::LOOP_END) ? start_pos_from_col(view_, new_col)
	                                                            : end_pos_from_col(view_, new_col);
	if (new_pos < 0) {
		new_pos = 0;
	}

	if (offset >= 0) {
		if (marker_type_ == SampleMarker::END && allow_extra_scroll_right()) {
			if (new_pos < old_pos) {
				return;
			}
		}
		else {
			if (new_pos > static_cast<int64_t>(view_.length())) {
				new_pos = static_cast<int64_t>(view_.length());
			}
		}
	}

	write_value(new_pos);

	// If marker was on-screen...
	if (old_col >= 0 && old_col < kMainCols) {
		cols = pins();
		// It might have changed, and despite having a new_col variable above, that's only our
		// desired value - we might have run into the end of the sample
		new_col = column_or_far_left(cols[idx(marker_type_)]);

		// But isn't anymore...
		if (new_col < 0 || new_col >= kMainCols) {
			view_ = view_.moved(offset);
			record_scroll_and_zoom();
		}
	}

	blink_phase_ = 0;
	blink_accum_ms_ = 0;
}

void MarkerScene::pad_action(int32_t x, int32_t y, bool on) {
	// `SampleMarkerEditor::padAction`'s main-pad arms. The two sidebar arms are not
	// here: the audition column delegates to `instrumentClipView`, a root UI this layer
	// may not name, and the mute column exits -- which `handle()` files as an action,
	// since only it can return one.
	if (!on) {
		// Release: the held-marker gesture ends when its own pad goes up.
		if (holding_ && !holding_and_auditioning() && x == press_x_ && y == press_y_) {
			holding_ = false;
		}
		return;
	}

	const std::array<views::waveform::MarkerPin, kSampleMarkerCount> cols = pins();

	// See which one we pressed, including legacy's toggle between two markers that
	// share a column.
	std::optional<SampleMarker> marker_pressed;
	for (int32_t m = 0; m < static_cast<int32_t>(kSampleMarkerCount); m++) {
		if (column_or_far_left(cols[m]) == x) {
			if (marker_pressed.has_value()) {
				if (marker_at(m) != marker_type_) {
					marker_pressed = marker_at(m);
				}
			}
			else {
				marker_pressed = marker_at(m);
			}
		}
	}

	if (holding_) {
		// An audio clip has no loop markers, so legacy's whole held-marker arm of `SampleMarkerEditor::padAction` sits
		// inside `if (getCurrentClip()->type == ClipType::INSTRUMENT)` and simply falls
		// out of the bottom on an audio clip -- leaving `currentUIMode` set and doing nothing.
		if (ctx_.is_audio_clip) {
			return;
		}

		// See which one we were holding down.
		std::optional<SampleMarker> marker_held;
		for (int32_t m = 0; m < static_cast<int32_t>(kSampleMarkerCount); m++) {
			if (column_or_far_left(cols[m]) == press_x_) {
				marker_held = marker_at(m);
			}
		}

		if (marker_held == SampleMarker::START) {
			// Unless we actually just tapped the already existing loop point: remove it.
			if (x == column_or_far_left(cols[idx(SampleMarker::LOOP_START)])) {
				marker_type_ = SampleMarker::LOOP_START;
				set_loop_locked(false);
				write_value(0);
				marker_type_ = SampleMarker::START;
				return;
			}
			// Limit position.
			if (column_or_far_left(cols[idx(SampleMarker::START)]) >= x) {
				return;
			}
			if (cols[idx(SampleMarker::LOOP_END)].position != 0
			    && column_or_far_left(cols[idx(SampleMarker::LOOP_END)]) <= x) {
				return;
			}
			if (column_or_far_left(cols[idx(SampleMarker::END)]) <= x) {
				return;
			}
			const int64_t new_value = start_pos_from_col(view_, x);
			// Loop start and end points are not allowed to be further right than the waveform length
			if (new_value >= static_cast<int64_t>(view_.length())) {
				return;
			}
			marker_type_ = SampleMarker::LOOP_START;
			holding_ = false;
			blink_phase_ = 0;
			blink_accum_ms_ = 0;
			write_value(new_value);
			return;
		}

		if (marker_held == SampleMarker::END) {
			if (x == column_or_far_left(cols[idx(SampleMarker::LOOP_END)])) {
				marker_type_ = SampleMarker::LOOP_END;
				set_loop_locked(false);
				write_value(0);
				marker_type_ = SampleMarker::END;
				return;
			}
			if (column_or_far_left(cols[idx(SampleMarker::START)]) >= x) {
				return;
			}
			if (column_or_far_left(cols[idx(SampleMarker::LOOP_START)]) >= x) {
				return; // Will be a big negative number if inactive
			}
			if (column_or_far_left(cols[idx(SampleMarker::END)]) <= x) {
				return;
			}
			const int64_t new_value = end_pos_from_col(view_, x);
			if (new_value >= static_cast<int64_t>(view_.length())) {
				return;
			}
			marker_type_ = SampleMarker::LOOP_END;
			holding_ = false;
			blink_phase_ = 0;
			blink_accum_ms_ = 0;
			write_value(new_value);
			return;
		}

		if (marker_held == SampleMarker::LOOP_START && marker_pressed == SampleMarker::LOOP_END) {
			// Toggle loop lock.
			set_loop_locked(!loop_locked());
			return;
		}

		if (marker_held == SampleMarker::LOOP_START) {
			// Or if a loop point and they pressed the end marker, remove the loop point.
			if (x == column_or_far_left(cols[idx(SampleMarker::START)])) {
				set_loop_locked(false);
				write_value(0);
				marker_type_ = SampleMarker::START;
				holding_ = false;
				blink_phase_ = 1;
				blink_accum_ms_ = 0;
			}
			return;
		}

		if (marker_held == SampleMarker::LOOP_END) {
			if (x == column_or_far_left(cols[idx(SampleMarker::END)])) {
				set_loop_locked(false);
				write_value(0);
				marker_type_ = SampleMarker::END;
				holding_ = false;
				blink_phase_ = 1;
				blink_accum_ms_ = 0;
			}
			return;
		}

		// Main's `SampleMarkerEditor::padAction` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`) falls out
		// of the bottom here with `markerHeld == NONE` and clears the mode before writing an UNINITIALISED `value` --
		// the one path in `padAction` that reads `value` without having assigned it. This scene refuses the press.
		holding_ = false;
		return;
	}

	// Or if user not already holding a marker down...
	if (marker_pressed.has_value()) {
		// If we tapped a marker.
		blink_phase_ = (marker_type_ == *marker_pressed) ? 0 : 1;
		blink_accum_ms_ = 0;
		marker_type_ = *marker_pressed;
		holding_ = true;
		audition_under_hold_ = auditioning();
		press_x_ = static_cast<int8_t>(x);
		press_y_ = static_cast<int8_t>(y);
		return;
	}

	// Otherwise, move the current marker to where we tapped. Make sure it doesn't go
	// past any other markers it shouldn't.
	if (marker_type_ == SampleMarker::START) {
		if (cols[idx(SampleMarker::LOOP_START)].position != 0
		    && column_or_far_left(cols[idx(SampleMarker::LOOP_START)]) <= x) {
			return;
		}
		if (cols[idx(SampleMarker::LOOP_END)].position != 0
		    && column_or_far_left(cols[idx(SampleMarker::LOOP_END)]) <= x) {
			return;
		}
		if (column_or_far_left(cols[idx(SampleMarker::END)]) <= x) {
			return;
		}
	}
	else if (marker_type_ == SampleMarker::LOOP_START) {
		if (column_or_far_left(cols[idx(SampleMarker::START)]) >= x) {
			return;
		}
		if (cols[idx(SampleMarker::LOOP_END)].position != 0
		    && column_or_far_left(cols[idx(SampleMarker::LOOP_END)]) <= x) {
			return;
		}
		if (column_or_far_left(cols[idx(SampleMarker::END)]) <= x) {
			return;
		}
	}
	else if (marker_type_ == SampleMarker::LOOP_END) {
		if (column_or_far_left(cols[idx(SampleMarker::START)]) >= x) {
			return;
		}
		if (column_or_far_left(cols[idx(SampleMarker::LOOP_START)]) >= x) {
			return; // Will be a big negative number if inactive
		}
		if (column_or_far_left(cols[idx(SampleMarker::END)]) <= x) {
			return;
		}
	}
	else if (marker_type_ == SampleMarker::END) {
		if (column_or_far_left(cols[idx(SampleMarker::START)]) >= x) {
			return;
		}
		if (column_or_far_left(cols[idx(SampleMarker::LOOP_START)]) >= x) {
			return;
		}
		if (column_or_far_left(cols[idx(SampleMarker::LOOP_END)]) >= x) {
			return;
		}
	}

	int64_t value = (marker_type_ < SampleMarker::LOOP_END) ? start_pos_from_col(view_, x) : end_pos_from_col(view_, x);

	{
		const auto length = static_cast<int64_t>(view_.length());
		// Only the END marker, and only in some cases, is allowed to be further right than the
		// waveform length
		if (marker_type_ == SampleMarker::END && allow_extra_scroll_right()) {
			if (x > column_or_far_left(cols[idx(marker_type_)]) && value < cols[idx(marker_type_)].position) {
				return; // Probably not actually necessary
			}
			if (value > length && value < length + view_.zoom()) {
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
	write_value(value);
}

Result MarkerScene::horizontal_encoder(int32_t offset, bool zooming, bool shift) {
	Result r;
	r.handled = true;

	// `SampleMarkerEditor::horizontalEncoderAction`'s shift arm: halve or double the
	// locked loop.
	if (loop_locked() && shift) {
		const Zone z = zone();
		int64_t proposed_end = z.loop_end;
		const int64_t loop_length = int64_t{z.loop_end} - z.loop_start;

		if (offset > 0) { // turn clockwise
			proposed_end = int64_t{z.loop_start} + 2 * loop_length;
			if (proposed_end <= static_cast<int64_t>(z.end) && proposed_end <= kLastLoopPoint) {
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
				proposed_end = int64_t{z.loop_start} + (loop_length >> 1);
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
		set_loop_locked(false, /*announce=*/false);
		write_value(proposed_end, SampleMarker::LOOP_END);
		set_loop_locked(true, /*announce=*/false);
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}

	// The marker a zoom pins (the selected one), and the end marker a scroll may reach past the waveform's end.
	const std::array<views::waveform::MarkerPin, kSampleMarkerCount> cols = pins();
	const std::optional<size_t> preferred = static_cast<size_t>(idx(marker_type_));
	const std::optional<int64_t> end_column = cols[idx(SampleMarker::END)].column;

	// Zoom with X_ENC held, scroll without, and either only with no mode up but X_ENC held and auditioning
	// (`SampleMarkerEditor::horizontalEncoderAction`'s `zoomUIModes`): never while a marker is held. X_ENC cannot be
	// held with a marker (each refuses the other), so only the scroll asks.
	bool success = false;
	if (sliding()) {
		// `UI_MODE_HORIZONTAL_ZOOM` is in neither of `zoomUIModes`' ranges: a turn while a zoom slides is dropped.
	}
	else if (zooming) {
		// The arithmetic, then the slide from the old view to the new one, pinned at the square whose position both
		// views share (`origin/main:src/deluge/gui/waveform/waveform_basic_navigator.cpp`,
		// `WaveformBasicNavigator::zoom`). With no marker selected, `preferred` is none and no pin is weighed.
		const int64_t old_scroll = view_.scroll();
		const int64_t old_zoom = view_.zoom();
		const std::optional<views::waveform::WaveformView> zoomed =
		    view_.zoomed(offset, cols, preferred, allow_extra_scroll_right());
		success = zoomed.has_value();
		if (success) {
			view_ = *zoomed;
			// A file the card holds is under 2^32 frames, so its zoom is at most 2^28 and fits.
			const auto slide_zoom = [](int64_t zoom) {
				return static_cast<int32_t>(std::clamp<int64_t>(zoom, 1, std::numeric_limits<int32_t>::max()));
			};
			const int32_t pin =
			    static_cast<int32_t>(base::trunc_div((old_scroll - view_.scroll()) << 16, view_.zoom() - old_zoom));
			zoom_pending_ = deluge::ui::gfx::grid::ZoomParams{
			    .old_zoom = slide_zoom(old_zoom),
			    .new_zoom = slide_zoom(view_.zoom()),
			    .pin_square_from = pin,
			    .pin_square_to = pin,
			    .t = 0,
			    // Legacy's `|offset|`; an encoder delivers one detent per event, so the bound never bites, but it is
			    // the step `gfx::grid::zoom` can take.
			    .magnitude = std::min<int32_t>(std::abs(offset), deluge::ui::gfx::grid::kMaxZoomMagnitude),
			};
		}
	}
	else if (!holding_) {
		const std::optional<views::waveform::WaveformView> scrolled =
		    view_.scrolled(offset, allow_extra_scroll_right(), end_column);
		success = scrolled.has_value();
		if (success) {
			view_ = *scrolled;
		}
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
	if (!auditioning()) {
		audition_under_hold_ = false;
	}
	// Input reaches the scene before the tick's render, which is where the waveform is otherwise followed; a sample
	// swapped under the range since the last frame must already bound what this event does.
	if (const std::optional<bind::WaveformInfo> info = waveform_info()) {
		follow_waveform(*info);
	}

	switch (event.kind) {
	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::widgets::core::EncoderRole::Select) {
			// Applied here, before the tick's render, so the frame that follows already shows the move.
			select_encoder(event.delta);
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
			return r;
		}
		if (event.encoder == deluge::ui::widgets::core::EncoderRole::Horizontal) {
			// Legacy zooms while the horizontal encoder's own BUTTON is held
			// (`UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`, set by `SampleMarkerEditor::buttonAction`'s `X_ENC` arm);
			// the shell tracks held buttons for us, so the mode is a question rather than a member.
			return horizontal_encoder(event.delta, xenc_held_, ctx.shift);
		}
		// A vertical turn is the host's (`forwardMarkerVertical`), which reaches the clip view this layer may not
		// name.
		return Result::unhandled();

	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		if (event.button == deluge::ui::shell::ButtonId::Back) {
			// `SampleMarkerEditor::buttonAction`'s BACK arm (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`):
			// a press with no UI mode up exits.
			r.handled = true;
			if (event.on && !mode_up()) {
				(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kMarkerSceneKind});
			}
			return r;
		}
		if (event.button == deluge::ui::shell::ButtonId::XEnc) {
			// `SampleMarkerEditor::buttonAction`'s X_ENC arm: held only from no mode or auditioning alone; released
			// always.
			r.handled = true;
			if (!event.on) {
				xenc_held_ = false;
			}
			else if (!holding_ && !xenc_held_ && !sliding()) {
				xenc_held_ = true;
			}
			return r;
		}
		return Result::unhandled();

	case UiEvent::Kind::Pad: {
		const int32_t x = event.x;
		if (x == kMainCols) {
			// The mute column: legacy exits on a press with no UI mode up.
			if (event.on && !mode_up()) {
				r.handled = true;
				(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kMarkerSceneKind});
			}
			else {
				r.handled = true;
			}
			return r;
		}
		if (x > kMainCols) {
			// The audition column is the host's (`grid_pad_claim`); one that arrives anyway is swallowed, as legacy
			// swallows it on an audio clip.
			r.handled = true;
			return r;
		}
		// `padAction` -> `potentialShortcutPadAction` with SHIFT held: under a marker hold, X_ENC or a slide the mode
		// is not one `shortcutPadUIModes` lists, and the press is swallowed; auditioning alone goes on to the pad. With
		// no mode up a shortcut with an item leaves the editor for that item's page (`changeUIAtLevel(&soundEditor,
		// 1)`), and any other is swallowed, the editor staying.
		const bool only_auditioning = auditioning() && !holding_ && !xenc_held_ && !sliding();
		// With the automation view as the root, its interpolation and pad-selection pads (x 0, y 6 and 7) are no
		// shortcut (`potentialShortcutPadAction`'s `ignoreAction`): with SHIFT they go on to the editor's own arms
		// below, under any mode.
		const bool root_keeps_pad = ctx_.root_is_automation && x == 0 && (event.y == 6 || event.y == 7);
		if (event.on && ctx.shift && !root_keeps_pad && x < kMainCols && !only_auditioning) {
			r.handled = true;
			if (!mode_up()) {
				if (const std::optional<SceneId> page =
				        menu_scene().shortcut_jump(static_cast<uint8_t>(x), static_cast<uint8_t>(event.y))) {
					(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kMarkerSceneKind});
					(void)r.actions.push_back(Action{.kind = Action::Kind::Replace, .id = *page});
				}
			}
			return r;
		}
		// A press under any mode but a held marker or auditioning is refused.
		if (event.on && (xenc_held_ || sliding() || holding_and_auditioning())) {
			r.handled = true;
			return r;
		}
		pad_action(x, event.y, event.on);
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		if (pending_popup_.has_value()) {
			(void)r.actions.push_back(Action{.kind = Action::Kind::Popup, .popup_string = sid(*pending_popup_)});
			pending_popup_.reset();
		}
		return r;
	}

	// The MIDI kinds (the transport in `ui/shell/scene.h`) are declined here for the reason
	// every scene declines them: claiming one means answering `learnable_at(pad)`,
	// which is the timeline scenes' work. Declining lets `Router::run_chain` carry the
	// message on to `services::MidiLearn`, which is where legacy's `view.` fallback puts it.
	case UiEvent::Kind::MidiNoteOn:
	case UiEvent::Kind::MidiCc:
	case UiEvent::Kind::MidiProgramChange:
	case UiEvent::Kind::MidiPitchBend:
	case UiEvent::Kind::AllReleased:
		return Result::unhandled();
	}
	return Result::unhandled();
}

// ---------------------------------------------------------------------------------------------
// Time

void MarkerScene::tick(uint32_t dt_ms) {
	if (!marker_on_screen_ || sliding()) {
		// `timerCallback`'s early return: no marker visible, no blink; and `WaveformBasicNavigator::zoom`
		// unsets its timer for the slide.
		return;
	}
	blink_accum_ms_ += dt_ms;
	while (blink_accum_ms_ >= kBlinkPhaseMs) {
		blink_accum_ms_ -= kBlinkPhaseMs;
		blink_phase_ = static_cast<int8_t>((blink_phase_ + 1) % 4);
	}
}

std::optional<uint32_t> MarkerScene::next_timer() const {
	std::optional<uint32_t> next;
	if (marker_on_screen_) {
		next = kBlinkPhaseMs - std::min(blink_accum_ms_, kBlinkPhaseMs);
	}
	// `graphicsRoutine` runs every pass: while a voice plays, and for the frame that clears the playhead it leaves.
	if (playing_ || (play_cursor() != deluge::project::FlashCursor::OFF && play_position().has_value())) {
		next = next.has_value() ? std::min(*next, kPlayheadMs) : kPlayheadMs;
	}
	return next;
}

// ---------------------------------------------------------------------------------------------
// Rendering

void MarkerScene::render_oled(SurfaceRole role, deluge::ui::widgets::oled::OledFrame& f) {
	(void)role;
	const std::optional<bind::WaveformInfo> info = waveform_info();
	if (!info) {
		return;
	}

	const std::array<views::waveform::MarkerPin, kSampleMarkerCount> cols = pins();
	const auto pos = static_cast<uint64_t>(cols[idx(marker_type_)].position);

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
	deluge::ui::widgets::oled::title(f, label);

	if (loop_locked()) {
		deluge::ui::widgets::oled::title_badge(f, deluge::ui::gfx::oled::icons::markerLock);
	}

	deluge::ui::widgets::oled::sample_position(
	    f,
	    deluge::ui::gfx::oled::Rect{1, body_y(f), f.bounds().w - 1,
	                                2 * deluge::ui::widgets::oled::kSamplePositionLineGap},
	    pos, info->sample_rate);
}

void MarkerScene::render_grid(deluge::ui::widgets::grid::GridFrame& f) {
	const std::optional<bind::WaveformInfo> info = waveform_info();
	if (!info) {
		// Nothing to slide or to play: a slide left running would hold every input refused.
		zoom_pending_.reset();
		zoom_.active = false;
		playing_ = false;
		return;
	}
	follow_waveform(*info);

	const int32_t main_cols = std::min<int32_t>(f.bounds().w, kMainCols);
	const ui::gfx::grid::GridDimensions main_dims{static_cast<uint8_t>(main_cols), 0,
	                                              static_cast<uint8_t>(f.bounds().h)};

	// The zoom slide (`PadLEDs::renderZoom`), over the main pads alone. It starts from the last settled frame and
	// slides to the new view's bare waveform (`renderFullScreen` into the other `imageStore` half), drawn here into a
	// grid of its own; the markers and the playhead come back with the settled frame after it.
	if (zoom_pending_.has_value()) {
		const ui::gfx::grid::ZoomParams params = *zoom_pending_;
		zoom_pending_.reset();
		if (shown_.has_value() && shown_->dims() == main_dims) {
			ui::gfx::grid::PadGrid& to = deluge::ui::widgets::grid::begin_zoom(zoom_, *shown_, params, f.now_ms());
			deluge::ui::widgets::grid::GridFrame bare = deluge::ui::widgets::grid::root_grid_frame(
			    to, f.ctx, deluge::ui::widgets::core::InputMask::none(), nullptr, nullptr, true);
			if (const std::optional<services::WaveformFrame> wf =
			        services::read_waveform(*waveforms(), where_, view_.window())) {
				services::paint_columns(bare, wf->read, wf->centre, wf->span);
			}
		}
	}
	if (zoom_.active) {
		bool slid = false;
		f.region(deluge::ui::gfx::grid::Rect{0, 0, main_cols, f.bounds().h},
		         [&](deluge::ui::widgets::grid::GridFrame& main) {
			         slid = deluge::ui::widgets::grid::zoom_transition(main, zoom_);
		         });
		if (slid) {
			playing_ = false;
			return;
		}
	}

	// `renderMainPads`' `waveformRenderer.renderFullScreen`, scaled by the range after the read. A column the card
	// could not give paints black this frame, and the frame asks again for it.
	f.region(
	    deluge::ui::gfx::grid::Rect{0, 0, main_cols, f.bounds().h}, [&](deluge::ui::widgets::grid::GridFrame& main) {
		    if (const std::optional<services::WaveformFrame> wf =
		            services::read_waveform(*waveforms(), where_, view_.window())) {
			    services::paint_columns(main, wf->read, wf->centre, wf->span);
			    if (retry_.after(wf->read, view_.window(), f.now_ms()) == services::PeakRetry::Next::ASK_AGAIN) {
				    f.grid_gate().request_repaint_after(services::kPeakRetryPollMs, f.now_ms());
			    }
		    }

		    const std::array<views::waveform::MarkerPin, kSampleMarkerCount> cols = pins();

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

		    // Colours are resolved here rather than in the widget: `reverseRemap` reads
		    // `SampleControls`, which `src/ui` may not include.
		    std::array<RGB, kSampleMarkerCount> colours{};
		    for (int32_t m = 0; m < static_cast<int32_t>(kSampleMarkerCount); m++) {
			    colours[static_cast<size_t>(m)] = kMarkerColours[static_cast<size_t>(idx(reverse_remap(marker_at(m))))];
		    }

		    for (int32_t col = 0; col < main_cols; col++) {
			    if (col == selected_col && blink_phase_ == 1) {
				    // Phase 1: the whole column flashes the selected marker's colour.
				    deluge::ui::widgets::grid::marker_tint(main, col, 0, main.bounds().h,
				                                           colours[static_cast<size_t>(idx(marker_type_))], false);
				    continue;
			    }
			    uint32_t active = 0;
			    for (int32_t m = 0; m < static_cast<int32_t>(kSampleMarkerCount); m++) {
				    if (column_or_far_left(cols[m]) == col) {
					    active |= 1u << m;
				    }
			    }
			    if (active == 0) {
				    continue;
			    }
			    deluge::ui::widgets::grid::marker_column(
			        main, col,
			        deluge::ui::widgets::grid::MarkerStack{
			            .active = active, .suppress = suppress, .selected = idx(marker_type_)},
			        colours);
		    }

		    // `renderMainPads`' timer re-arm: the blink only runs while the selected
		    // marker is actually inside the display.
		    marker_on_screen_ = selected_col >= 0 && selected_col < main_cols;
		    if (marker_on_screen_) {
			    // `tick()` advancing `blink_phase_` changes nothing anyone can see unless the surface
			    // is asked for another frame, and `next_timer()` alone only wakes the shell without
			    // marking the pads dirty (`RepaintGate::needs_frame`). `Shell::tick`'s step 8b arms
			    // this same gate from `next_timer()` itself, so the blink does not depend on this line
			    // -- it is kept as the tighter statement (armed from the frame being drawn, and only
			    // while the marker is on screen). Legacy has no equivalent either way, because
			    // its `timerCallback` paints straight into `PadLEDs::image` itself.
			    f.gate().request_repaint_after(kBlinkPhaseMs, f.now_ms());
		    }
	    });

	if (!shown_.has_value() || shown_->dims() != main_dims) {
		shown_.emplace(main_dims);
	}
	for (int32_t y = 0; y < main_dims.rows; y++) {
		for (int32_t x = 0; x < main_cols; x++) {
			shown_->set(static_cast<uint8_t>(x), static_cast<uint8_t>(y), f.peek(x, y));
		}
	}

	// `graphicsRoutine`'s playhead: nothing with the flash cursor Off; else the column the newest voice
	// plays, on every row, painted with it Slow and flashed by the PIC with it Fast (`PadLEDs::setTickSquares`).
	const deluge::project::FlashCursor cursor = play_cursor();
	playing_ = false;
	if (cursor == deluge::project::FlashCursor::OFF) {
		return;
	}
	std::array<deluge::ui::widgets::grid::TickSquare, kRows> per_row{};
	const std::optional<int64_t> pos = play_position();
	playing_ = pos.has_value();
	if (pos.has_value() && *pos >= view_.scroll()) {
		const int64_t col = base::floor_div(*pos - view_.scroll(), view_.zoom());
		if (col < main_cols) {
			per_row.fill(deluge::ui::widgets::grid::TickSquare{
			    .col = static_cast<uint8_t>(col), .colour = deluge::ui::widgets::grid::TickColour::Normal});
		}
	}
	if (cursor == deluge::project::FlashCursor::SLOW) {
		deluge::ui::widgets::grid::tick_squares(f, per_row, deluge::gui::colours::black);
	}
	deluge::ui::widgets::grid::flash_tick_squares(f, per_row, last_ticks_,
	                                              cursor == deluge::project::FlashCursor::FAST);
}

std::optional<bind::WaveformInfo> MarkerScene::waveform_info() const {
	const bind::Waveforms* w = waveforms();
	return w != nullptr ? w->info(where_) : std::nullopt;
}

std::optional<int64_t> MarkerScene::play_position() const {
	const bind::Waveforms* w = waveforms();
	return w != nullptr ? w->play_position(where_) : std::nullopt;
}

deluge::project::FlashCursor MarkerScene::play_cursor() const {
	return bind::read_device<deluge::project::FlashCursor>(ctx_.ports.device,
	                                                       deluge::project::device_path::kPlayCursor);
}

MarkerScene& marker_scene() {
	static MarkerScene scene;
	return scene;
}

} // namespace deluge::ui::scenes::marker
