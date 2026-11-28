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
///        `audio_clip_scene.h` for the predicates, the playhead rule and the sidebar.

#include "ui/scenes/audio_clip/audio_clip_scene.h"
#include "ui/scenes/services/global_controls.h"

#include "base/divide.h"
#include "base/notify/notice_queue.h"
#include "music/note_length.h"
#include "project/timeline_limits.h"
#include "ui/gfx/grid/palette.h"
#include "ui/widgets/core/led_frame.h"
#include "ui/widgets/grid/columns.h"
#include "ui/widgets/grid/sidebar.h"
#include "ui/widgets/grid/waveform.h"
#include "ui/widgets/oled/misc.h"
#include "ui/widgets/oled/scroll_text.h"
#include "ui/widgets/oled/text.h"

#include "ui/scenes/bind/ports/root_views.h"
#include "ui/scenes/bind/ports/waveforms.h"

#include "gui/l10n/strings.h"
#include "project/audio_input.h"
#include "project/device_settings.h"
#include "project/features.h"
#include "project/setting_group.h"
#include "project/song.h"
#include "project/track_kind.h"
#include "serialization/tree.h"
#include "ui/gfx/grid/exp_table.h"
#include "ui/gfx/grid/pad_grid.h"
#include "ui/scenes/services/waveform_paint.h"
#include "ui/views/grid/scroll_slide.h"
#include "ui/views/grid/waveform.h"
#include "ui/views/grid/zoom_slide.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace deluge::ui::scenes::audio_clip {

using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using deluge::ui::widgets::grid::GridFrame;
using deluge::ui::widgets::oled::OledFrame;

/// @brief `deluge::ui::shell::Action` -- the shell's navigation request. Spelled through this alias
///        so `::Action` (the legacy undo-log entry, `model/action/action.h`) can keep its own name
///        in the length machinery below without either one being ambiguous.
using ShellAction = deluge::ui::shell::Action;
using deluge::ui::scenes::bind::ResizeKind;

namespace {

/// @brief One l10n string id, as `Action::popup_string` wants it. `marker_scene.cpp`'s `sid`.
constexpr uint16_t sid(deluge::l10n::String s) {
	return static_cast<uint16_t>(s);
}

/// @brief `renderMainPads`' fixed 16 main columns (`origin/main:src/deluge/gui/views/audio_clip_view.cpp` writes
///        `image[y][0 .. kDisplayWidth)` throughout).
constexpr int32_t kMainCols = deluge::ui::gfx::grid::kDelugeGrid.main_cols;
constexpr int32_t kRows = deluge::ui::gfx::grid::kDelugeGrid.rows;

} // namespace

AudioClipScene& audio_clip_scene() {
	static AudioClipScene scene;
	return scene;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle

void AudioClipScene::open(std::optional<deluge::project::ClipAddress> clip, const deluge::project::Song* model) {
	clip_ = model != nullptr ? clip : std::nullopt;
	model_ = model;
	retry_.restart();
	adopt_navigation();
}

void AudioClipScene::adopt_navigation() {
	if (model_ != nullptr) {
		if (const deluge::ui::scenes::bind::ClipFacts* c = clips(); c != nullptr) {
			if (const std::optional<deluge::ui::scenes::bind::ClipNavigation> n = c->clip_navigation()) {
				timeline_.adopt(*n);
			}
		}
		// `adopt` copies the song's triplets setting, but `supportsTriplets()` is a property of the VIEW
		// (`AudioClipView::supportsTriplets` returned false) and is not in the song. Restored after every adopt, so
		// `in_triplets_view()` answers what the view answered.
		timeline_.supports_triplets = false;
	}
	published_ = {.x_scroll = timeline_.x_scroll, .x_zoom = timeline_.x_zoom};
}

void AudioClipScene::navigation_restored() {
	const int32_t before_scroll = timeline_.x_scroll;
	const uint32_t before_zoom = timeline_.x_zoom;
	adopt_navigation();
	note_slide(before_scroll, before_zoom);
}

// ---------------------------------------------------------------------------------------------
// The installed ports

const deluge::ui::scenes::bind::ClipFacts* AudioClipScene::clips() const {
	return bind::port_of(ports_, &bind::Ports::clips);
}

deluge::ui::scenes::bind::ClipWrites* AudioClipScene::clip_writes() const {
	return bind::port_of(ports_, &bind::Ports::clip_writes);
}

deluge::ui::scenes::bind::RootViews* AudioClipScene::root_views() const {
	return bind::port_of(ports_, &bind::Ports::root_views);
}

deluge::ui::scenes::bind::SessionMacros* AudioClipScene::macros() const {
	return bind::port_of(ports_, &bind::Ports::macros);
}

deluge::ui::scenes::bind::WriteSink* AudioClipScene::writes() const {
	return bind::port_of(ports_, &bind::Ports::writes);
}

const deluge::ui::scenes::bind::DeviceSettings* AudioClipScene::device() const {
	return bind::port_of(ports_, &bind::Ports::device);
}

const deluge::ui::scenes::bind::Transport* AudioClipScene::transport() const {
	return bind::port_of(ports_, &bind::Ports::transport);
}

deluge::ui::scenes::bind::TransportActions* AudioClipScene::transport_actions() const {
	return bind::port_of(ports_, &bind::Ports::transport_actions);
}

const deluge::ui::scenes::bind::Waveforms* AudioClipScene::waveforms() const {
	return bind::port_of(ports_, &bind::Ports::waveforms);
}

// ---------------------------------------------------------------------------------------------
// What the scene reads: the clip's record, and the facts no record holds

const deluge::project::DocClip* AudioClipScene::doc() const {
	return clip_ && model_ != nullptr ? model_->resolveClip(*clip_) : nullptr;
}

int32_t AudioClipScene::loop_length() const {
	const deluge::project::DocClip* record = doc();
	return record != nullptr ? record->loop_length : 0;
}

std::optional<deluge::ui::scenes::bind::AudioClipFact> AudioClipScene::fact() const {
	const deluge::ui::scenes::bind::ClipFacts* c = clips();
	return clip_ && c != nullptr ? c->audio_clip(*clip_) : std::nullopt;
}

bool AudioClipScene::recording_linearly() const {
	const deluge::ui::scenes::bind::ClipFacts* c = clips();
	return clip_ && c != nullptr && c->clip_recording_linearly(*clip_);
}

deluge::ui::scenes::bind::WaveformAddress AudioClipScene::waveform() const {
	return {.kind = deluge::ui::scenes::bind::WaveformAddress::Kind::AUDIO_CLIP,
	        .clip = clip_.value_or(deluge::project::ClipAddress{})};
}

bool AudioClipScene::has_sample() const {
	// `getSample()` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`): the take while recording linearly, else
	// the file.
	const deluge::ui::scenes::bind::Waveforms* w = waveforms();
	return clip_ && w != nullptr && w->info(waveform()).has_value();
}

std::optional<deluge::project::TrackAddress> AudioClipScene::track() const {
	const std::optional<deluge::ui::scenes::bind::AudioClipFact> f = fact();
	return f ? std::optional{deluge::project::TrackAddress{f->track_id}} : std::nullopt;
}

std::string AudioClipScene::track_row(deluge::project::SettingGroupId group, std::string_view path) const {
	const std::optional<deluge::project::TrackAddress> at = track();
	if (!at || model_ == nullptr) {
		return {};
	}
	const std::optional<deluge::serialization::Value> value =
	    deluge::project::read_setting(*model_, {.group = group, .owner = *at}, path);
	return value ? value->str() : std::string{};
}

int32_t AudioClipScene::audio_mode() const {
	const std::string word = track_row(deluge::project::SettingGroupId::AUDIO_INPUT, "mode");
	for (size_t i = 0; i < std::size(deluge::project::kAudioModeWords); i++) {
		if (deluge::project::kAudioModeWords[i] == word) {
			return static_cast<int32_t>(i);
		}
	}
	return 0;
}

bool AudioClipScene::on_enter(const SceneId& id) {
	(void)id;
	// `AudioClipView::opened` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`). `uiNeedsRendering(this)` is the
	// shell's business in immediate mode, and `getCurrentClip()->onAutomationClipView = false`
	// is a flag `AutomationView` reads -- not ported, because `automationView` is on
	// `scenes-no-root-ui`'s forbidden list and nothing in this layer reads the flag either. What is
	// left is the tick-square latch and `focusRegained`, in legacy's own order.
	must_redraw_tick_squares_ = true;
	flashed_ticks_ = filled_no_tick(); // the PIC's latch, as `clearTickSquares` leaves it between views
	// An entry starts with no button latch: every way into the view leaves legacy's mode word clear, and the scene is
	// one object for every entry, so a latch left by the last visit (X held as the view was left) must not survive it.
	// A resume keeps it: a menu opened and closed over the view leaves a held X held.
	ui_mode_ = kUiModeNone;
	// Nor a slide: the view opens settled, with nothing on the pads yet to slide from.
	finish_slides();
	shown_.reset();
	focus_regained();
	return true;
}

void AudioClipScene::on_resume() {
	focus_regained();
}

void AudioClipScene::focus_regained() {
	// `AudioClipView::focusRegained` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`); see the private
	// declaration for the four statements that have no counterpart in this layer.
	//
	// `ClipView::focusRegained` calls
	// `ClipNavigationTimelineView::focusRegained` (`clip_navigation_timeline_view.cpp`),
	// whose whole body is the restore-point clear -- `TimelineState::focus_regained()` -- and then resets the
	// shortcut-overview latch and asks for a redraw, which the shell owns.
	timeline_.focus_regained();
	// `ClipView::focusRegained`: returning from a shortcut's menu starts a fresh overview if SHIFT is still active.
	overview_exited_ = false;
	end_marker_visible_ = false;
	start_marker_visible_ = false;
	// **And nothing else.** `blink_on_` and `blink_accum_ms_` are deliberately NOT reset here:
	// legacy's `AudioClipView::focusRegained` touches `blinkOn` on none of its
	// paths. It does not have to -- with both markers down nothing
	// reads the phase, and the one gesture that raises a marker restarts it, which is `padAction`'s
	// own `blinkOn = true; setTimer(UI_SPECIFIC, kSampleMarkerBlinkTime)` pair in its two marker-raising arms
	// and is ported as `set_end_marker_visible`/`set_start_marker_visible`. Unobservable
	// either way.
}

// ---------------------------------------------------------------------------------------------
// The two marker latches, and the three model questions that branch on them

void AudioClipScene::set_end_marker_visible(bool on) {
	if (end_marker_visible_ == on) {
		return;
	}
	end_marker_visible_ = on;
	// `padAction` restarts the blink whenever it raises a marker
	// (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`, `blinkOn = true;` then `setTimer(UI_SPECIFIC,
	// kSampleMarkerBlinkTime)`), so the bright half is always the one the user sees first.
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
	// `AudioClipView::getMaxLength`.
	if (!clip_) {
		return 0;
	}
	if (end_marker_visible_) {
		return static_cast<uint32_t>(loop_length()) + 1;
	}
	return static_cast<uint32_t>(loop_length());
}

uint32_t AudioClipScene::max_zoom() const {
	// `AudioClipView::getMaxZoom`. The 1073741824 is legacy's own
	// literal, not `kMaxZoom`: it is the largest value that can be doubled inside an `int32_t`.
	const std::optional<deluge::ui::scenes::bind::AudioClipFact> f = fact();
	if (!f) {
		return 0;
	}
	int32_t max = f->max_zoom;
	if (end_marker_visible_ && max < 1073741824) {
		max <<= 1;
	}
	return static_cast<uint32_t>(max);
}

bool AudioClipScene::scrollable_and_zoomable() const {
	// `AudioClipView::setupScroll`'s guard (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`):
	// `currentlyScrollableAndZoomable`, a take recorded without the clock (or in the arranger) locks the view.
	const deluge::ui::scenes::bind::Transport* t = transport();
	const bool clocks = t != nullptr && t->clocks_active();
	const bool arrangement = t != nullptr && t->arrangement_mode();
	return clip_.has_value() && !(recording_linearly() && (!clocks || arrangement));
}

// ---------------------------------------------------------------------------------------------
// Input

Result AudioClipScene::handle(const UiEvent& event, const UiContext& ctx) {
	const int32_t before_scroll = timeline_.x_scroll;
	const uint32_t before_zoom = timeline_.x_zoom;
	// `maybeStartShortcutOverview` and `stopShortcutOverview` run first in the arms that call them.
	note_overview_input(event, ctx.shift);
	Result r = dispatch(event, ctx);
	note_slide(before_scroll, before_zoom);
	publish_navigation();
	overview_shown_ = overview_wanted(ctx.shift);
	return r;
}

void AudioClipScene::note_overview_input(const UiEvent& event, bool shift) {
	using deluge::ui::shell::ButtonId;
	// Every button reaches the view's `buttonAction` first (`Buttons::buttonAction` in
	// `origin/main:src/deluge/hid/buttons.cpp`), the encoders' own buttons too.
	if (event.kind == UiEvent::Kind::Button || event.kind == UiEvent::Kind::EncoderButton) {
		const ButtonId b = event.button;
		// Instrument changes keep the overview and refresh it for the new instrument; both SHIFT edges restart it,
		// because releasing the physical SHIFT can leave it latched.
		const bool changing_instrument =
		    shift && (b == ButtonId::Synth || b == ButtonId::Kit || b == ButtonId::Midi || b == ButtonId::Cv);
		if ((b == ButtonId::Shift || changing_instrument) && overview_setting_on()) {
			overview_exited_ = false;
		}
		else if (event.on) {
			overview_exited_ = true;
		}
	}
	else if (event.kind == UiEvent::Kind::Encoder
	         && (event.encoder == deluge::ui::widgets::core::EncoderRole::Horizontal
	             || event.encoder == deluge::ui::widgets::core::EncoderRole::Vertical)) {
		overview_exited_ = true;
	}
}

bool AudioClipScene::overview_setting_on() const {
	return bind::read_device<deluge::project::ShortcutOverlay>(
	           device(), deluge::project::device_path::feature(deluge::project::Feature::SHORTCUT_OVERLAY))
	       == deluge::project::ShortcutOverlay::MENU_AND_CLIP;
}

bool AudioClipScene::overview_wanted(bool shift) const {
	return shift && !overview_exited_ && overview_setting_on();
}

deluge::ui::widgets::grid::GreyoutMask AudioClipScene::greyout_mask() const {
	// `ClipView::getGreyoutColsAndRows`: every column, the sidebar's too, and no row.
	return overview_shown_ ? deluge::ui::widgets::grid::GreyoutMask{.cols = 0xFFFFFFFFu, .rows = 0}
	                       : deluge::ui::widgets::grid::GreyoutMask{};
}

void AudioClipScene::note_slide(int32_t before_scroll, uint32_t before_zoom) {
	if (timeline_.x_zoom != before_zoom) {
		// `initiateXZoom`'s pin square (`TimelineView::calculateZoomPinSquares`): where the two views line up.
		const int64_t moved = static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(before_scroll)
		                                                                - static_cast<uint32_t>(timeline_.x_scroll)))
		                      << 16;
		const int32_t pin = static_cast<int32_t>(
		    base::trunc_div(moved, static_cast<int64_t>(timeline_.x_zoom) - static_cast<int64_t>(before_zoom)));
		pending_slide_ = PendingSlide{.kind = PendingSlide::Kind::ZOOM,
		                              .zoom = deluge::ui::gfx::grid::ZoomParams{
		                                  .old_zoom = static_cast<int32_t>(before_zoom),
		                                  .new_zoom = static_cast<int32_t>(timeline_.x_zoom),
		                                  .pin_square_from = pin,
		                                  .pin_square_to = pin,
		                                  .t = 0,
		                              }};
		enter_ui_mode(kUiModeHorizontalZoom);
	}
	else if (timeline_.x_scroll != before_scroll && scrollable_and_zoomable()) {
		// `initiateXScroll`, behind `setupScroll`'s guard: a take recorded with no clock does not slide.
		pending_slide_ = PendingSlide{.kind = PendingSlide::Kind::SCROLL,
		                              .direction = static_cast<int8_t>(timeline_.x_scroll > before_scroll ? 1 : -1)};
		enter_ui_mode(kUiModeHorizontalScroll);
	}
}

void AudioClipScene::finish_slides() {
	pending_slide_ = PendingSlide{};
	scroll_.active = false;
	zoom_.active = false;
	exit_ui_mode(kUiModeHorizontalScroll);
	exit_ui_mode(kUiModeHorizontalZoom);
}

void AudioClipScene::publish_navigation() {
	// The view is the song's clip navigation, as legacy's timeline views keep theirs in the song's persistent view: the
	// next visit opens on it and the song file saves it. Only a move is written.
	if (timeline_.x_scroll == published_.x_scroll && timeline_.x_zoom == published_.x_zoom) {
		return;
	}
	published_ = {.x_scroll = timeline_.x_scroll, .x_zoom = timeline_.x_zoom};
	if (deluge::ui::scenes::bind::ClipWrites* w = clip_writes(); w != nullptr && model_ != nullptr) {
		w->set_clip_navigation(timeline_.x_scroll, timeline_.x_zoom);
	}
}

Result AudioClipScene::dispatch(const UiEvent& event, const UiContext& ctx) {
	switch (event.kind) {
	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		// One arm for both kinds, because legacy has one: `Buttons::buttonAction`
		// (`hid/buttons.cpp`) hands `X_ENC`, `Y_ENC`, `SELECT_ENC` and `TEMPO_ENC` to
		// `getCurrentUI()->buttonAction` through the same call the ordinary buttons take, and
		// `AudioClipView::buttonAction` claims two of them. `services::GlobalControls::handle`
		// accepts the same pair for the same reason.
		return button_action(event, ctx);

	case UiEvent::Kind::Pad:
		return pad_action(event, ctx);

	case UiEvent::Kind::Encoder:
		switch (event.encoder) {
		case deluge::ui::widgets::core::EncoderRole::Select: {
			// `AudioClipView::selectEncoderAction` returns void:
			// legacy's `Encoders` calls it on the current UI and there is no "not dealt with".
			// Applied here, before the tick's render.
			Result r;
			r.handled = true;
			if (select_encoder_action(event.delta, ctx.shift)) {
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
			return r;
		}
		case deluge::ui::widgets::core::EncoderRole::Horizontal:
			return horizontal_encoder_action(event.delta, ctx);
		case deluge::ui::widgets::core::EncoderRole::Vertical:
			return vertical_encoder_action(event.delta, ctx);
		default:
			// The two gold knobs and the tempo encoder are `services::ModKnobs`'s and
			// `services::GlobalControls`'s; `AudioClipView` overrides none of their hooks.
			return Result::unhandled();
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
		break;
	}
	return Result::unhandled();
}

// -- The UI-mode word (`gui/ui/ui.cpp`), transcribed ------------------------------------

void AudioClipScene::enter_ui_mode(uint32_t mode) {
	// `enterUIMode` (`gui/ui/ui.cpp`).
	if (mode > kExclusiveUiModesMask) {
		ui_mode_ |= mode;
	}
	else {
		ui_mode_ = (ui_mode_ & ~kExclusiveUiModesMask) | mode;
	}
}

void AudioClipScene::exit_ui_mode(uint32_t mode) {
	// `exitUIMode` (`gui/ui/ui.cpp`).
	if (mode > kExclusiveUiModesMask) {
		ui_mode_ &= ~mode;
	}
	else if ((ui_mode_ & kExclusiveUiModesMask) == mode) {
		ui_mode_ &= ~kExclusiveUiModesMask;
	}
}

bool AudioClipScene::ui_mode_active(uint32_t mode) const {
	// `isUIModeActive` (`gui/ui/ui.cpp`).
	if (mode > kExclusiveUiModesMask) {
		return (ui_mode_ & mode) != 0u;
	}
	return (ui_mode_ & kExclusiveUiModesMask) == mode;
}

// -- Buttons ------------------------------------------------------------------------------------

bool AudioClipScene::deactivate_marker_if_necessary() {
	// `buttonAction`'s `deactivateMarkerIfNecessary` tail (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`). The
	// `getCurrentUI() == this` guard on the timer unset has no counterpart -- a `Scene`
	// cannot ask whether it is the stack's input top, the same gap `compute_tick_square`'s
	// `ui_idle` records -- and it guards only *which* of the two timer owners cancels the blink,
	// which in this port is one object either way.
	if (!end_marker_visible_) {
		return false;
	}
	set_end_marker_visible(false);
	return true;
}

uint16_t AudioClipScene::clear_clip() {
	// `buttonAction`'s `BACK` arm's body (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`), minus `main`'s
	// card-routine defer, which the shell answers with `InputLowering::set_sd_busy` rather than
	// by asking the scene (`ui/shell/scene.h`'s `Action` warning).
	deluge::ui::scenes::bind::ClipWrites* edits = clip_writes();
	if (!clip_ || edits == nullptr) {
		return 0;
	}
	// The device's automation-clear row, read once for both the request and the popup.
	const bool clear = bind::read_device_flag(device(), deluge::project::device_path::kAutomationClear);
	// `AudioClipView::buttonAction`'s clear, as one request: the clear and its undo step.
	(void)edits->clear_clip(*clip_, !clear, true);
	// Legacy's two popups, chosen by the same flag that chose the argument above.
	const uint16_t popup = clear ? sid(deluge::l10n::String::STRING_FOR_SAMPLE_CLEARED)
	                             : sid(deluge::l10n::String::STRING_FOR_CLIP_CLEARED);
	// `AudioClipView::buttonAction`'s `endMarkerVisible = false` and blink-timer unset: the setter
	// resets the blink state with the marker.
	set_end_marker_visible(false);
	return popup;
}

Result AudioClipScene::button_action(const UiEvent& event, const UiContext& ctx) {
	// `AudioClipView::buttonAction` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`), arm for arm and in
	// legacy's own order, because the order is what decides which arm claims a button.
	Result r;
	const ButtonId b = event.button;
	const bool on = event.on;

	// ---- SESSION_VIEW ----
	if (b == ButtonId::Session) {
		r.handled = true; // every path of this arm reaches the DEALT_WITH at the end of `AudioClipView::buttonAction`
		if (on) {
			if (ui_mode_ == kUiModeNone) {
				ui_mode_ = kUiModeHoldingSongButton;
				// Legacy measures in `AudioEngine::audioSampleTimer`; this layer has no
				// clock (`shell-no-clock`) and takes the shell's `now_ms`, exactly as
				// `services::GlobalControls`' SAVE/LOAD arms do.
				song_button_pressed_ms_ = ctx.now_ms;
				// The `indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, true)` here is `leds()`', which lights
				// SESSION while the mode holds it.
				// The `uiNeedsRendering(this, 0, 0xFFFFFFFF)` after it is the SIDEBAR only; `RepaintAll`
				// is the only `Action` that reaches the grid at all (`ui/shell/scene.h`'s table).
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
			return r;
		}
		if (!ui_mode_active(kUiModeHoldingSongButton)) {
			return r;
		}
		exit_ui_mode(kUiModeHoldingSongButton);
		if (static_cast<uint32_t>(ctx.now_ms - song_button_pressed_ms_) > timeline::kShortPressMs) {
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			return r;
		}
		// `uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC)` and `ClipMinder::transitionToArrangerOrSession()`, both
		// the host's.
		if (deluge::ui::scenes::bind::RootViews* v = root_views()) {
			v->to_session_or_arranger();
		}
		return r;
	}

	// ---- CLIP_VIEW ----
	if (b == ButtonId::Clip) {
		// `if (on && currentUIMode == UI_MODE_NONE) changeRootUI(&automationView)`, the root change the host's. Claimed
		// on every path, because legacy falls through to the closing DEALT_WITH of `AudioClipView::buttonAction`.
		r.handled = true;
		if (on && no_ui_mode_active()) {
			if (deluge::ui::scenes::bind::RootViews* v = root_views()) {
				v->to_automation();
			}
		}
		return r;
	}

	// ---- PLAY / RECORD / SHIFT ----
	if (b == ButtonId::Play || b == ButtonId::Record || b == ButtonId::Shift) {
		// `goto dontDeactivateMarker` -> `return ClipView::buttonAction(...)`, which for these three resolves through
		// `TimelineView::buttonAction`'s `else` (`timeline_view.cpp`) to `view.buttonAction` --
		// `services::GlobalControls`. Unhandled is exactly that hand-off, **and the marker is deliberately not
		// deactivated**: that is what the `dontDeactivateMarker` label buys, since these three return before
		// `AudioClipView::buttonAction`'s `deactivateMarkerIfNecessary` tail.
		return Result::unhandled();
	}

	// ---- X_ENC ----
	if (b == ButtonId::XEnc) {
		r.handled = true;
		if (ctx.held.has(ButtonId::YEnc)) {
			if (on && ui_mode_ == kUiModeNone) {
				clip_length_to_sample_length();
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
			return r;
		}
		if (ctx.shift) {
			// The `X_ENC` arm's `else if (!Buttons::isShiftButtonPressed())` is false, so the arm does
			// nothing and -- crucially -- never delegates. That is what leaves
			// `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` unset while shift+X_ENC is held, which is
			// what makes `horizontalEncoderAction`'s `isNoUIModeActive()` first arm reachable at all.
			// See the class docstring.
			return r;
		}
		// The `X_ENC` arm's `goto dontDeactivateMarker` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`) leads
		// to `ClipView::buttonAction`
		// (`clip_view.cpp`).
		if (on && !scrollable_and_zoomable()) {
			// Its first arm is an empty block, then DEALT_WITH -- so a clip that cannot be
			// zoomed does not enter the mode at all.
			return r;
		}
		// `TimelineView::buttonAction`'s X_ENC arm (`gui/views/timeline_view.cpp`).
		if (on) {
			if (no_ui_mode_active()) {
				show_zoom_level();
			}
			enter_ui_mode(kUiModeHoldingHorizontalEncoder);
		}
		else if (ui_mode_active(kUiModeHoldingHorizontalEncoder)) {
			deluge::base::notify::notice_kind(deluge::base::notify::Notice::Kind::CancelPopup);
			exit_ui_mode(kUiModeHoldingHorizontalEncoder);
		}
		return r;
	}

	// ---- SELECT_ENC without shift (`AudioClipView::buttonAction` in
	// `origin/main:src/deluge/gui/views/audio_clip_view.cpp`) ----
	if (b == ButtonId::SelectEnc && !ctx.shift) {
		// `openMenu(getCurrentClip(), ...)`, site 5 of `gui/ui/menu_entry.cpp`'s table, the host's; a menu that opens
		// takes the end marker down (`goto deactivateMarkerIfNecessary`), one that refuses leaves it. Claimed on every
		// path: legacy's arm ends in the closing DEALT_WITH.
		r.handled = true;
		if (on && no_ui_mode_active()) {
			if (deluge::ui::scenes::bind::RootViews* v = root_views(); v != nullptr) {
				v->open_clip_menu();
			}
		}
		return r;
	}

	// ---- BACK while the horizontal encoder button is held ----
	// **The exact comparison is legacy's**: with the song button also held, `ui_mode_` is
	// `kUiModeHoldingSongButton | kUiModeHoldingHorizontalEncoder` and this arm does not match, so
	// `BACK` goes back to being undo. See the class docstring for why this arm is load-bearing.
	if (b == ButtonId::Back && ui_mode_ == kUiModeHoldingHorizontalEncoder) {
		r.handled = true; // claimed on release too -- the `if (on)` of legacy's `BACK` arm guards the body, not the arm
		if (on) {
			const uint16_t popup = clear_clip();
			if (popup != 0) {
				(void)r.actions.push_back(
				    ShellAction{.kind = ShellAction::Kind::Popup, .surface = SurfaceRole::Main, .popup_string = popup});
			}
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
		}
		return r;
	}

	// ---- everything else ----
	// `ClipMinder::buttonAction` (`model/clip/clip_minder.cpp`) is `return NOT_DEALT_WITH;`
	// and nothing else, so this reduces to `ClipView::buttonAction` -> (no override) ->
	// `TimelineView::buttonAction`, whose only remaining arm is TRIPLETS.
	if (b == ButtonId::Triplets && on) {
		// The `TRIPLETS` arm of the `TimelineView::buttonAction` above (`timeline_view.cpp`).
		// `TimelineView::setTripletsLEDState` leaves TRIPLETS off for an audio clip, which the root view's focus does.
		timeline_.toggle_triplets();
		r.handled = true;
	}
	// The tail of `AudioClipView::buttonAction` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`) sits after the
	// `if (result == NOT_DEALT_WITH)` call to `ClipView::buttonAction`, so it runs whether or not the arm above claimed
	// the button.
	if (deactivate_marker_if_necessary()) {
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
	}
	return r;
}

// -- Pads ---------------------------------------------------------------------------------------

Result AudioClipScene::pad_action(const UiEvent& event, const UiContext& ctx) {
	// `AudioClipView::padAction`. Every path of it that is not one of `main`'s card-routine defers returns DEALT_WITH
	// (the sidebar arm's included), so this claims every pad it is given.
	Result r;
	r.handled = true;
	const int32_t x = event.x;
	const bool on = event.on;

	if (x >= kMainCols) {
		// The first sidebar column, and only while the song button is held.
		if (x == kMainCols && ui_mode_active(kUiModeHoldingSongButton)) {
			// A press redraws the column with the held pad brightened; the release runs the macro
			// (`view.activateMacro(y)`) with the two buttons it reads, and redraws it dimmed again.
			if (!on && event.y < deluge::ui::scenes::bind::SessionMacros::kSlots) {
				if (deluge::ui::scenes::bind::SessionMacros* m = macros()) {
					m->activate(event.y, {.affect_entire = ctx.held.has(ButtonId::AffectEntire), .shift = ctx.shift});
				}
			}
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
		}
		return r;
	}

	if (ctx.held.has(ButtonId::TempoEnc)) {
		if (on && clip_) {
			if (deluge::ui::scenes::bind::TransportActions* a = transport_actions()) {
				a->grab_tempo(*clip_);
			}
		}
		return r;
	}

	// `main`'s `sdRoutineLock` defer in `AudioClipView::padAction`'s main-pad arm, after the TEMPO_ENC grab, has no
	// counterpart: the shell's SD gate is `InputLowering::set_sd_busy`, which defers and retries rather than asking a
	// scene (`ui/shell/scene.h`'s `Action` warning names that split).
	//
	// `menuShortcutPadAction(x, y, on)`, press and release alike (its first statement writes a latch on either): the
	// host decides from the shortcut settings and the menu's tables whether the pad is a shortcut. A pad the menu
	// takes puts BOTH markers down and runs nothing else.
	if (deluge::ui::scenes::bind::RootViews* v = root_views();
	    v != nullptr && v->menu_shortcut_pad(event.x, event.y, on)) {
		set_end_marker_visible(false);
		set_start_marker_visible(false);
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
		return r;
	}

	if (!on || !no_ui_mode_active()) {
		return r;
	}
	if (!clip_) {
		return r;
	}

	const int32_t zoom = static_cast<int32_t>(timeline_.x_zoom == 0 ? 1 : timeline_.x_zoom);
	// Both squares come from the scene's own scroll and zoom, which `open()` adopted
	// from the same `Song` slot legacy reads.
	const int32_t end_square = timeline::divide_round_negative(loop_length() - timeline_.x_scroll - 1, zoom);
	const int32_t start_square = timeline::divide_round_negative(0 - timeline_.x_scroll, zoom);

	const auto repaint = [&r] { (void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll}); };

	if (end_marker_visible_) {
		if (x == end_square || x == start_square) {
			// Tapping either edge column puts the marker back down.
			set_end_marker_visible(false);
			repaint();
			return r;
		}
		if (deluge::ui::scenes::bind::ClipWrites* edits = clip_writes(); has_sample() && edits != nullptr) {
			const int32_t new_length = (x + 1) * zoom + timeline_.x_scroll;
			// The end marker and the length, as one request.
			(void)edits->resize_audio_clip(*clip_, ResizeKind::END, new_length, loop_length());
			repaint();
		}
		return r;
	}

	if (start_marker_visible_) {
		if (x == start_square || x == end_square) {
			// Tapping either edge column puts the start marker back down.
			set_start_marker_visible(false);
			repaint();
			return r;
		}
		if (deluge::ui::scenes::bind::ClipWrites* edits = clip_writes(); has_sample() && edits != nullptr) {
			const int32_t new_start_ticks = x * zoom + timeline_.x_scroll;
			// The start marker and the length, as one request.
			(void)edits->resize_audio_clip(*clip_, ResizeKind::START, new_start_ticks, loop_length());
			repaint();
		}
		return r;
	}

	// No marker up. Are we on (or just past) the end column, or on the start column?
	if (x == end_square || x == end_square + 1) {
		// `set_end_marker_visible(true)` is `endMarkerVisible = true` plus the `blinkOn = true` and the timer arm of
		// that marker-raising arm; `startMarkerVisible` is already false in this branch, so the setter below is a no-op
		// that says so.
		set_end_marker_visible(true);
		set_start_marker_visible(false);
		repaint();
	}
	else if (x == start_square
	         && bind::read_device_flag(device(), deluge::project::device_path::feature(
	                                                 deluge::project::Feature::TRIM_FROM_START_OF_AUDIO_CLIP))) {
		// Legacy's own work-in-progress arm, behind a runtime feature toggle that is on by default
		// (`project::kFeatures`).
		set_start_marker_visible(true);
		set_end_marker_visible(false);
		repaint();
	}
	return r;
}

// -- Encoders -----------------------------------------------------------------------------------

bool AudioClipScene::select_encoder_action(int32_t offset, bool shift) {
	// `AudioClipView::selectEncoderAction`.
	if (!no_ui_mode_active()) {
		return false;
	}
	if (shift) {
		// `View::navigateThroughAudioOutputsForAudioClip`, the host's: it moves the clip to another output, which the
		// OLED names.
		if (deluge::ui::scenes::bind::RootViews* v = root_views()) {
			v->next_audio_output(static_cast<int8_t>(offset));
			return true;
		}
		return false;
	}
	// `AudioClipView::selectEncoderAction`'s `else` branch (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`):
	// the track's mode, scrolled as `AudioOutput::scrolledMode` scrolls it, written to the track's record.
	const std::optional<deluge::project::TrackAddress> at = track();
	deluge::ui::scenes::bind::WriteSink* sink = writes();
	if (!at || sink == nullptr) {
		return false;
	}
	constexpr auto kModes = static_cast<int32_t>(std::size(deluge::project::kAudioModeWords));
	const int32_t mode = std::clamp((audio_mode() + static_cast<int8_t>(offset)) % kModes, 0, kModes - 1);
	deluge::ui::scenes::bind::Addresses where{};
	where.track = *at;
	(void)sink->write({.target = {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
	                              .path = "mode",
	                              .group = deluge::project::SettingGroupId::AUDIO_INPUT},
	                   .where = where,
	                   .menu_value = mode});
	return true;
}

Result AudioClipScene::vertical_encoder_action(int32_t offset, const UiContext& ctx) {
	// `AudioClipView::verticalEncoderAction`.
	Result r;
	// DEALT_WITH on every path of `AudioClipView::verticalEncoderAction` that is not one of `main`'s card-routine
	// defers, claimed or not.
	r.handled = true;
	if (no_ui_mode_active() && ctx.shift && !ctx.held.has(ButtonId::YEnc)) {
		// `main`'s card-routine defer in `AudioClipView::verticalEncoderAction` has no counterpart here (see
		// `pad_action`).
		if (deluge::ui::scenes::bind::ClipWrites* edits = clip_writes(); clip_ && edits != nullptr) {
			(void)edits->recolour_clip(*clip_, offset);
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
		}
	}
	return r;
}

Result AudioClipScene::horizontal_encoder_action(int32_t offset, const UiContext& ctx) {
	// `AudioClipView::horizontalEncoderAction` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`).
	if (no_ui_mode_active() && ctx.held.has(ButtonId::XEnc) && ctx.shift) {
		return edit_clip_length_without_timestretching(offset);
	}
	return clip_horizontal_encoder_action(offset, ctx);
}

Result AudioClipScene::clip_horizontal_encoder_action(int32_t offset, const UiContext& ctx) {
	// `ClipView::horizontalEncoderAction` (`gui/views/clip_view.cpp`), three arms.
	Result r;
	r.handled = true; // every arm ends in DEALT_WITH

	// ---- shift: edit the length, WITH timestretching (`ClipView::horizontalEncoderAction`'s first arm, in
	// `clip_view.cpp`) ----
	if (no_ui_mode_active() && !ctx.held.has(ButtonId::YEnc) && ctx.shift) {
		if (!scrollable_and_zoomable()) {
			(void)r.actions.push_back(
			    ShellAction{.kind = ShellAction::Kind::Popup,
			                .surface = SurfaceRole::Main,
			                .popup_string = sid(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH)});
			return r;
		}
		const auto old_length = static_cast<uint32_t>(loop_length());
		if (timeline_.scroll_right_to_end_of_length_if_necessary(static_cast<int32_t>(old_length))) {
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			return r;
		}
		// `main`'s SD defer in `ClipView::horizontalEncoderAction`: none, as above.
		const uint32_t new_length = change_clip_length(offset, old_length, ResizeKind::STRETCH);
		show_bars_and_beats(new_length, /*count_from_one=*/false);
		note_undo_scroll();
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
		return r;
	}

	// ---- Y_ENC (or X_ENC held + CLIP): shift the clip's contents (`ClipView::horizontalEncoderAction`'s second arm,
	// in `clip_view.cpp`) ----
	if ((no_ui_mode_active() && ctx.held.has(ButtonId::YEnc))
	    || (ui_mode_ == kUiModeHoldingHorizontalEncoder && ctx.held.has(ButtonId::Clip))) {
		// The clip's contents by whole squares at the current zoom, through the clip port, which applies legacy's
		// `ShiftClip` with its undo step. Claimed either way: legacy's arm returns, so a Y_ENC turn never reaches
		// `GlobalControls`.
		const int32_t square = timeline_.pos_from_square(1) - timeline_.pos_from_square(0);
		if (deluge::ui::scenes::bind::ClipWrites* edits = clip_writes(); clip_ && edits != nullptr) {
			if (edits->shift_clip_horizontally(*clip_, offset * square)) {
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
		}
		return r;
	}

	// ---- otherwise, scroll and zoom (`ClipView::horizontalEncoderAction`'s last arm, in `clip_view.cpp`) ----
	if (!scrollable_and_zoomable()) {
		return r;
	}
	return nav_horizontal_encoder_action(offset, ctx);
}

Result AudioClipScene::nav_horizontal_encoder_action(int32_t offset, const UiContext& ctx) {
	// `ClipNavigationTimelineView::horizontalEncoderAction`
	// (`gui/views/clip_navigation_timeline_view.cpp`).
	//
	// **First statement, above every gate below it** -- `xScrollBeforeFollowingAutoExtendingLinearRecording = -1` --
	// which is the whole reason `TimelineState::stop_following()` is public. `zoom()` and `scroll()` call it too, so on
	// a turn that moves something this is redundant; on a turn the gates below refuse it is the only call, and legacy
	// clears there just the same. See the class docstring for why it is here and not in `horizontal_encoder_action`
	// above.
	timeline_.stop_following();

	Result r;
	// `TimelineView::horizontalEncoderAction`'s last `return` (`timeline_view.cpp`): DEALT_WITH on every path that
	// is not a defer.
	r.handled = true;

	// `TimelineView::horizontalEncoderAction`'s re-entry guards (`pendingUIRenderingLock`,
	// `horizontalEncoderActionLock`, which drop a turn that finds one held) and `main`'s `sdRoutineLock` defer above
	// them have no counterpart: the SD gate is `InputLowering::set_sd_busy`'s, and an immediate-mode scene called once
	// per event cannot re-enter.
	//
	// `TimelineView::horizontalEncoderAction`'s `inNoteRowEditor` is `deluge::gui::ui::noteRowEditorOpen()` -- a
	// `gui/ui` read this layer may not make, and false for this view in legacy too unless a note-row editor is open
	// over it. Constant false here, which is what makes both `|| inNoteRowEditor` disjuncts below drop out.

	if (ui_mode_active(kUiModeHoldingHorizontalEncoder)) {
		// Exclusively.
		if (ui_mode_ == kUiModeHoldingHorizontalEncoder) {
			// `TimelineView::horizontalEncoderAction`'s zoom arm, whose state half is `TimelineState::zoom` -- the
			// max-zoom delay, the zoom limits and the scroll realignment included.
			const timeline::ZoomOutcome outcome = timeline_.zoom(offset, max_zoom(), ctx.now_ms);
			if (outcome == timeline::ZoomOutcome::Zoomed) {
				show_zoom_level();
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
		}
		return r;
	}

	// Minus `UI_MODE_AUDITIONING`, which this scene cannot enter.
	if (no_ui_mode_active()) {
		if (!ctx.shift) {
			const bool moved = timeline_.scroll(offset, max_length());
			if (moved) {
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
			// `displayScrollPos()` in `TimelineView::horizontalEncoderAction` sits OUTSIDE the
			// `if (newXScroll != ...)`, so legacy shows the position even on a turn that moved nothing.
			// Read from the enclosing braces, not from the shape: the outer `if` closes after the inner
			// one, and the call sits after both, inside the `if (!shift)` block.
			show_bars_and_beats(static_cast<uint32_t>(timeline_.x_scroll), /*count_from_one=*/true);
		}
	}
	return r;
}

// -- The length gestures -------------------------------------------------------------------------
//
// Each is one request: the app applies it as one Edit, which owns the undo step -- its Action and coalescing, the
// marker's move, the length and the re-sync. What stays here is what the view does around it: the new length's
// arithmetic from the view's squares, and the scroll and zoom after.

uint32_t AudioClipScene::change_clip_length(int32_t offset, uint32_t old_length, ResizeKind kind) {
	// `ClipView::changeClipLength`.
	bool right_on_square = false;
	uint32_t new_length = 0;
	const int32_t end_square = timeline_.square_from_pos(static_cast<int32_t>(old_length), &right_on_square);
	const bool lengthens = offset > 0;
	if (lengthens) {
		new_length =
		    static_cast<uint32_t>(timeline_.pos_from_square(end_square) + timeline_.length_extend_amount(end_square));
	}
	else {
		// Landing exactly on a column edge chops the previous square's width; landing
		// inside one snaps back to that column's start.
		new_length = right_on_square ? old_length - static_cast<uint32_t>(timeline_.length_chop_amount(end_square))
		                             : static_cast<uint32_t>(timeline_.pos_from_square(end_square));
	}
	// Only a length a clip may have is asked for (`ClipView::changeClipLength`'s two arms).
	const bool changes = lengthens ? new_length <= static_cast<uint32_t>(project::kMaxSequenceLength) : new_length > 0;
	if (deluge::ui::scenes::bind::ClipWrites* edits = clip_writes(); changes && edits != nullptr) {
		(void)edits->resize_audio_clip(*clip_, kind, static_cast<int32_t>(new_length),
		                               static_cast<int32_t>(old_length));
	}
	if (changes) {
		if (lengthens) {
			// The scroll happens either way; only the redraw is conditional, and the redraw is the
			// caller's `RepaintAll`.
			(void)timeline_.scroll_right_to_end_of_length_if_necessary(static_cast<int32_t>(new_length));
		}
		else if (!timeline_.scroll_left_if_too_far_right(static_cast<int32_t>(new_length))) {
			(void)timeline_.zoom_to_max(max_zoom(), /*in_only=*/true);
		}
	}
	return new_length;
}

Result AudioClipScene::edit_clip_length_without_timestretching(int32_t offset) {
	// `AudioClipView::editClipLengthWithoutTimestretching`.
	//
	// A DISTINCT arm from the shift-turn length edit: this one moves the sample's own end marker with the
	// clip, so the audio keeps its pitch and speed and gets longer or shorter; the other
	// moves only the clip and leaves the timestretch to absorb the difference.
	Result r;
	r.handled = true;
	if (!clip_) {
		return r;
	}
	if (!scrollable_and_zoomable()) {
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::Popup,
		                                      .surface = SurfaceRole::Main,
		                                      .popup_string = sid(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH)});
		return r;
	}
	if (timeline_.scroll_right_to_end_of_length_if_necessary(loop_length())) {
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
		return r;
	}
	// `main`'s SD defer in `AudioClipView::editClipLengthWithoutTimestretching`: none, as everywhere else in this file.

	// The length and the sample's end, which keeps the audio's speed, as one request.
	const uint32_t new_length =
	    change_clip_length(offset, static_cast<uint32_t>(loop_length()), ResizeKind::NO_STRETCH);

	show_bars_and_beats(new_length, /*count_from_one=*/false);
	note_undo_scroll();
	(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
	return r;
}

void AudioClipScene::note_undo_scroll() {
	// `action->xScrollClip[AFTER] = ...` after each length edit: the view scrolled after the change, so the step
	// learns where a redo returns to now.
	if (deluge::ui::scenes::bind::ClipWrites* edits = clip_writes(); clip_ && edits != nullptr) {
		edits->set_undo_scroll_after(*clip_, timeline_.x_scroll);
	}
}

void AudioClipScene::after_loop_length_change(int32_t old_length, int32_t new_length) {
	// `AudioClipView::adjustLoopLength`'s view half (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`): after a
	// shorten, scroll left if the view is now past the end, failing that zoom in to the clip's new max; then the new
	// length.
	if (new_length < old_length && new_length > 0 && !timeline_.scroll_left_if_too_far_right(new_length)) {
		(void)timeline_.zoom_to_max(max_zoom(), /*in_only=*/true);
	}
	show_bars_and_beats(static_cast<uint32_t>(new_length), /*count_from_one=*/false);
}

void AudioClipScene::clip_menu_opened() {
	if (deactivate_marker_if_necessary()) {
		deluge::base::notify::raise(deluge::base::notify::Dirty::GridMain);
	}
}

void AudioClipScene::set_clip_length_equal_to_sample_length() {
	// The menu's action, from outside `handle`: the edit, then what `handle` does after every input.
	const int32_t before_scroll = timeline_.x_scroll;
	const uint32_t before_zoom = timeline_.x_zoom;
	clip_length_to_sample_length();
	note_slide(before_scroll, before_zoom);
	publish_navigation();
}

void AudioClipScene::clip_length_to_sample_length() {
	// `AudioClipView::setClipLengthEqualToSampleLength`.
	deluge::ui::scenes::bind::ClipWrites* edits = clip_writes();
	if (!clip_ || edits == nullptr) {
		return;
	}
	const int32_t old_length = loop_length();
	// A clip with no sample to measure is refused, which legacy reports as no sample.
	if (!edits->resize_audio_clip(*clip_, ResizeKind::TO_SAMPLE_LENGTH, 0, old_length)) {
		deluge::base::notify::notice_string(sid(deluge::l10n::String::STRING_FOR_NO_SAMPLE));
		return;
	}
	after_loop_length_change(old_length, loop_length());
	note_undo_scroll();
	deluge::base::notify::notice_string(sid(deluge::l10n::String::STRING_FOR_CLIP_LENGTH_ADJUSTED));
}

void AudioClipScene::show_zoom_level() const {
	// The zoom as a note length per column ("16th-notes", "1 bar (per column)"), shown until X goes up: a warning
	// severity is the persistent text popup, and the X release cancels it.
	if (model_ == nullptr) {
		return;
	}
	const deluge::music::NoteLengthName name = deluge::music::note_length_name(
	    deluge::music::note_magnitude(timeline_.x_zoom, model_->transport.input_tick_magnitude()), "-notes", true);
	deluge::base::notify::Notice n{.kind = deluge::base::notify::Notice::Kind::Text,
	                               .severity = deluge::base::notify::Notice::Severity::Warn};
	const std::string_view text = name.view();
	const size_t len = std::min(text.size(), sizeof(n.text) - 1);
	std::copy_n(text.data(), len, n.text);
	n.text[len] = 0;
	(void)deluge::base::notify::notices().push(n);
}

void AudioClipScene::show_bars_and_beats(uint32_t number, bool count_from_one) {
	// `TimelineView::displayNumberOfBarsAndBeats`' popup half.
	// The decomposition is `TimelineState::bars_and_beats`, which records
	// that legacy's `quantization` and `tooLongText` parameters are read by nothing.
	if (model_ == nullptr) {
		return;
	}
	// `Song::getBarLength`: 96 ticks, by the song's input tick magnitude.
	const uint32_t one_bar =
	    static_cast<uint32_t>(deluge::ui::gfx::grid::increase_magnitude(96, model_->transport.input_tick_magnitude()));
	if (one_bar == 0) {
		return;
	}
	const timeline::BarsBeats b = timeline::TimelineState::bars_and_beats(number, one_bar, count_from_one);

	// `TimelineView::displayNumberOfBarsAndBeats`'s `sprintf(buffer, "%d : %d : %d",
	// ...)` then `popupTextTemporary`. Formatted by hand for `render_oled`'s reason -- `deluge::string::fromInt` is in
	// `util/`, which this layer may not include -- and raised through the notify seam because it is computed text
	// rather than an l10n id (`ui/scenes/slicing/slicer_scene.cpp` makes the same split).
	std::array<char, 15> buffer{}; // legacy's own `char buffer[15]`
	size_t len = 0;
	const auto append_int = [&](uint32_t v) {
		std::array<char, 12> digits{};
		size_t n = 0;
		do {
			digits[n++] = static_cast<char>('0' + (v % 10));
			v /= 10;
		} while (v != 0 && n < digits.size());
		while (n > 0 && len + 1 < buffer.size()) {
			buffer[len++] = digits[--n];
		}
	};
	const auto append = [&](std::string_view s) {
		for (const char c : s) {
			if (len + 1 < buffer.size()) {
				buffer[len++] = c;
			}
		}
	};
	append_int(b.bar);
	append(" : ");
	append_int(b.beat);
	append(" : ");
	append_int(b.sub_beat);
	deluge::base::notify::notice_text(std::string_view{buffer.data(), len});
}

// ---------------------------------------------------------------------------------------------
// Model notifications

void AudioClipScene::on_model_changed(const deluge::base::notify::DirtySet& dirty) {
	// `AudioClipView::clipNeedsReRendering`, `sampleNeedsReRendering` and `playbackEnded`. Two of the three are pure
	// `uiNeedsRendering` calls, which `Shell::apply_dirty` already does for these facts; what is left is the
	// scroll-back-left legacy hides inside `clipNeedsReRendering`.
	// `sampleNeedsReRendering`: a sample finished loading (an undo's, a claim's), so a waveform waiting on it is drawn.
	if (dirty.has(deluge::base::notify::Dirty::SampleReady) && clip_) {
		retry_.restart();
	}
	if (!dirty.has(deluge::base::notify::Dirty::ClipContent) || !clip_) {
		return;
	}
	// The clip's waveform may have changed (a sample attached, a take cleared): a redraw waiting on the old one stops.
	retry_.restart();
	// `AudioClipView::clipNeedsReRendering`'s `if (c == getCurrentAudioClip())`. The fact carries clip bits when the
	// raiser named one; a raise with no index means "which clips is unknown -- treat as all dirty"
	// (`base/notify/dirty_set.h`'s `has()` note), which is the over-invalidating side and so is safe to take as "this
	// clip".
	const deluge::base::notify::ClipBits& clips = dirty.clips(deluge::base::notify::Dirty::ClipContent);
	if (clips.any()) {
		const std::optional<deluge::ui::scenes::bind::AudioClipFact> f = fact();
		const int32_t index = f ? f->session_index : -1;
		if (index < 0 || !clips.test(static_cast<uint16_t>(index))) {
			return;
		}
	}
	// Legacy's own comment: "it seems it was a bit hacky that I put this in this
	// function". Ported where legacy has it, hack and all.
	if (timeline_.x_scroll >= loop_length()) {
		(void)timeline_.scroll_for_linear_recording(0, max_length());
	}
}

// ---------------------------------------------------------------------------------------------
// Time

void AudioClipScene::tick(uint32_t dt_ms) {
	// `AudioClipView::timerCallback`: toggle `blinkOn` and re-arm.
	// Legacy arms the timer only from `padAction`, when a marker goes up, and unsets it when one
	// goes down, so the toggle runs only while a marker is visible.
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
	if (end_marker_visible_ || start_marker_visible_) {
		return kMarkerBlinkMs - std::min(blink_accum_ms_, kMarkerBlinkMs);
	}
	return std::nullopt;
}

bool AudioClipScene::frame_driven() const {
	// `graphicsRoutine`'s own first gate, read positively (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`):
	// with no playback, no active clip, or a count-in running, the column is a constant 255 and nothing animates.
	const deluge::ui::scenes::bind::ClipFacts* c = clips();
	return flash_cursor() != project::FlashCursor::OFF && clip_ && c != nullptr && c->clip_playing(*clip_);
}

// ---------------------------------------------------------------------------------------------
// The playhead

int32_t AudioClipScene::compute_tick_square() {
	// `AudioClipView::graphicsRoutine` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`), minus the paint.
	//
	// The `UI_MODE_AUDIO_CLIP_COLLAPSING` early return, and the two animation
	// modes, are `PadLEDs` transitions the port does not carry at all -- see the class docstring.
	const deluge::ui::scenes::bind::ClipFacts* c = clips();
	if (!clip_ || c == nullptr) {
		return deluge::ui::widgets::grid::kNoTickSquare;
	}

	const bool recording = recording_linearly();
	const deluge::ui::scenes::bind::Transport* t = transport();
	const bool clocks = t != nullptr && t->clocks_active();
	const bool arrangement = t != nullptr && t->arrangement_mode();
	int32_t square = 0;
	if (!c->clip_playing(*clip_)) {
		// `AudioClipView::graphicsRoutine`'s first arm: no playback, or the clip is not active.
		square = deluge::ui::widgets::grid::kNoTickSquare;
	}
	else if (!clocks || (arrangement && recording)) {
		// Tempoless or arranger recording.
		square = kMainCols - 1;
		// While recording, the `needsRenderingDependingOnSubMode()` call there is a redraw request, which the shell
		// already grants every frame while the cursor is live (`frame_driven()`); the waveforms port reads the take's
		// new columns on every read.
	}
	else {
		// `getTickSquare()` is `ClipView::getTickSquare`,
		// whose state half is `TimelineState::tick_square`, including the `xZoom`-for-`xScroll` defect in its restore
		// arm.
		timeline::TimelineState::TickSquareGates gates{
		    .recording_linearly = recording,
		    // `(!currentUIMode || currentUIMode == UI_MODE_AUDITIONING) && getCurrentUI() == this`
		    // (`ClipView::getTickSquare` in `clip_view.cpp`). The port carries no UI modes on this scene, so the first
		    // half is true; the second has no counterpart -- a `Scene` cannot ask whether it is the
		    // stack's input top (`Scene::on_resume` fires on the edge, and `ui/shell/scene.h` records
		    // that there is deliberately no `on_pause` dual to close the bracket). So this reads
		    // true where legacy would read false with something open over the clip view, and the
		    // difference is confined to the auto-extending-linear-recording follow.
		    .ui_idle = true,
		    .clip_unarmed = fact().value_or(deluge::ui::scenes::bind::AudioClipFact{}).unarmed,
		    .max_length = max_length(),
		};
		const timeline::TimelineState::TickSquare tick = timeline_.tick_square(c->clip_live_pos(*clip_), gates);
		square = tick.square;
		if (square < 0 || square >= kMainCols) {
			square = deluge::ui::widgets::grid::kNoTickSquare;
		}
	}

	// The latch is `AudioClipView`'s own two private fields, which `timeline/timeline.h`'s
	// table assigns here by name.
	if (flash_cursor() != project::FlashCursor::OFF && (square != last_tick_square_ || must_redraw_tick_squares_)) {
		last_tick_square_ = square;
		must_redraw_tick_squares_ = false;
		tick_squares_set_ = true;
	}
	return last_tick_square_;
}

// ---------------------------------------------------------------------------------------------
// Rendering

void AudioClipScene::paint_main(GridFrame& main, GridFrame& retry_frame) {
	const deluge::ui::scenes::bind::Waveforms* shown = waveforms();
	const std::optional<deluge::ui::scenes::bind::WaveformInfo> info =
	    clip_ && shown != nullptr ? shown->info(waveform()) : std::nullopt;
	const std::optional<deluge::ui::scenes::bind::AudioClipFact> clip_fact = fact();
	// `renderMainPads`' two "just clear the display" arms: no sample, or no clip. The
	// frame arrives cleared, so there is nothing to erase -- but the two arms still `return`, which
	// means no waveform, no grey, no markers and (legacy's driver still substitutes) the cursor.
	const deluge::ui::scenes::bind::ClipFacts* facts = clips();
	if (!info || !clip_fact || facts == nullptr) {
		return;
	}
	const auto [scroll_samples, zoom_samples] =
	    facts->audio_clip_samples(*clip_, timeline_.x_scroll, static_cast<int32_t>(timeline_.x_zoom));

	// `AudioClipView::renderMainPads`'s `waveformRenderer.renderFullScreen`, split into its two halves: the scan
	// (model) and the paint (toolkit).
	//
	// The scan's failure is legacy's own: `renderFullScreen` returns **before its own
	// clear** (`origin/main:src/deluge/gui/waveform/waveform_renderer.cpp`), so a frame it could not load leaves
	// the image untouched and legacy asks to come back later. The port draws nothing for the
	// same reason -- `SlicerScene::render_grid` makes the identical call -- and asks again every frame until the
	// read is whole, as `AudioClipView::renderMainPads` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`)
	// does. `info` came through the port, so the port is here.
	const views::waveform::Window window{.scroll = scroll_samples, .zoom = zoom_samples};
	const std::optional<deluge::ui::scenes::services::WaveformFrame> wf =
	    deluge::ui::scenes::services::read_waveform(*shown, waveform(), window);
	if (wf
	    && retry_.after(wf->read, window, retry_frame.now_ms())
	           == deluge::ui::scenes::services::PeakRetry::Next::ASK_AGAIN) {
		retry_frame.grid_gate().request_repaint_after(deluge::ui::scenes::services::kPeakRetryPollMs,
		                                              retry_frame.now_ms());
	}
	if (!wf || wf->read.pending()) {
		return;
	}
	// `renderOneCol`'s coloured arm: `rgb` is engaged on this call site (`AudioClipView::renderMainPads`), so
	// `brightness` is 256 (`waveform_renderer.cpp`). Then the "grey regions + flashing columns" loop over it.
	const deluge::ui::views::grid::ClipMarkers markers = present_markers();
	deluge::ui::views::grid::waveform(main, present_waveform(*wf, markers, clip_fact->reversed),
	                                  deluge::ui::widgets::grid::kWaveformBarBrightnessColoured, present_colour());
	deluge::ui::views::grid::clip_markers(main, markers);
}

void AudioClipScene::render_grid(GridFrame& f) {
	// `graphicsRoutine` before the render, as the main loop runs it (`origin/main:src/deluge/deluge.cpp`'s loop calls
	// `UI::graphicsRoutine` before `doAnyPendingUIRendering`).
	const int32_t tick_col = compute_tick_square();

	const deluge::ui::gfx::grid::Rect main_rect{0, 0, kMainCols, f.bounds().h};
	const deluge::ui::gfx::grid::GridDimensions main_dims{static_cast<uint8_t>(kMainCols), 0,
	                                                      static_cast<uint8_t>(f.bounds().h)};

	overview_shown_ = overview_wanted(f.ctx.input.shift);
	if (overview_shown_) {
		// `maybeRenderShortcutsOverview` returns before the view draws itself; a slide in flight ends with it.
		finish_slides();
		f.region(main_rect, [&](GridFrame& main) { deluge::ui::views::grid::pads(main, present_overview()); });
	}
	// A slide the last input asked for starts from the last settled frame, and slides to the new view, painted into a
	// grid of its own (`renderMainPads` into `imageStore`, as `TimelineView::setupScroll` and `initiateXZoom` do).
	if (!overview_shown_ && pending_slide_.kind != PendingSlide::Kind::NONE) {
		const PendingSlide p = std::exchange(pending_slide_, PendingSlide{});
		if (shown_.has_value() && shown_->dims() == main_dims) {
			deluge::ui::gfx::grid::PadGrid& to =
			    p.kind == PendingSlide::Kind::SCROLL
			        ? deluge::ui::widgets::grid::begin_scroll(scroll_, *shown_, p.direction, f.now_ms())
			        : deluge::ui::widgets::grid::begin_zoom(zoom_, *shown_, p.zoom, f.now_ms());
			GridFrame bare = deluge::ui::widgets::grid::root_grid_frame(
			    to, f.ctx, deluge::ui::widgets::core::InputMask::none(), nullptr, nullptr, true);
			paint_main(bare, f);
		}
	}
	bool slid = false;
	if (scroll_.active || zoom_.active) {
		f.region(main_rect, [&](GridFrame& main) {
			slid = scroll_.active ? deluge::ui::views::grid::scroll_slide(main, scroll_)
			                      : deluge::ui::views::grid::zoom_slide(main, zoom_);
		});
	}
	if (!slid && !overview_shown_) {
		// No slide running, or its last step has passed: input is taken again, and the view draws itself.
		exit_ui_mode(kUiModeHorizontalScroll);
		exit_ui_mode(kUiModeHorizontalZoom);
		f.region(main_rect, [&](GridFrame& main) {
			paint_main(main, f);
			// What a slide starts from: the main columns as they settled, before the cursor goes over them.
			shown_.emplace(main_dims);
			for (int32_t y = 0; y < main_dims.rows; y++) {
				for (int32_t x = 0; x < kMainCols; x++) {
					shown_->set(static_cast<uint8_t>(x), static_cast<uint8_t>(y), main.peek(x, y));
				}
			}
		});
	}

	// `AudioClipView::renderSidebar`: both columns black, and the macros in the first while SESSION is held, each held
	// pad brightened; then `view.flashPlayEnable()` while any clip they name is armed (`bool armed =
	// view.clipArmFlashOn`, forced true by an armed clip: it is what brings the phase back to off after the last armed
	// clip disarms).
	std::array<bool, kRows> held{};
	for (int32_t y = 0; y < kRows; y++) {
		held[static_cast<size_t>(y)] = f.held_pads().test(kMainCols, y);
	}
	const deluge::ui::views::grid::ClipSidebar sidebar =
	    present_sidebar(ui_mode_active(kUiModeHoldingSongButton), held);
	deluge::ui::views::grid::clip_sidebar(f, kMainCols, sidebar);
	if (sidebar.macros && global_controls_ != nullptr
	    && std::ranges::any_of(sidebar.cells, deluge::ui::widgets::grid::macro_wants_arm_flash)) {
		global_controls_->flash_play_enable(f.now_ms());
	}

	// `graphicsRoutine`'s `PadLEDs::setTickSquares`, which the driver applies to the whole rendered image on its way
	// out (`PadLEDs::prepareColour`, `hid/led/pad_leds.cpp`) -- hence last, and over the root frame rather than the
	// main-column region.
	deluge::ui::views::grid::playhead(f, present_playhead(tick_col, std::exchange(tick_squares_set_, false)),
	                                  flashed_ticks_);
}

void AudioClipScene::leds(deluge::ui::widgets::core::LedFrame& frame) {
	using deluge::ui::widgets::core::LedState;
	constexpr LedState kOff{LedState::Kind::Off, 0, 0};
	constexpr LedState kOn{LedState::Kind::On, 0, 0};
	// LED ids are button ids, by the board profile's contract.
	(void)frame.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Back), kOff);
	(void)frame.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Session),
	                ui_mode_active(kUiModeHoldingSongButton) ? kOn : kOff);
}

void AudioClipScene::render_oled(SurfaceRole role, OledFrame& f) {
	(void)role;
	if (const std::optional<deluge::ui::views::oled::AudioClipName> name = present_name(name_text_)) {
		deluge::ui::views::oled::audio_clip_name(f, *name, name_scroll_, info_scroll_);
	}
}

} // namespace deluge::ui::scenes::audio_clip
