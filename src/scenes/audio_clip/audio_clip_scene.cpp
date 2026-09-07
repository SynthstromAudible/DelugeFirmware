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
#include "notify/notice_queue.h"
#include "scenes/menu/context.h"
#include "ui/grid/columns.h"
#include "ui/grid/sidebar.h"
#include "ui/grid/waveform.h"
#include "ui/oled/misc.h"
#include "ui/oled/text.h"

#include "gui/l10n/strings.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/consequence/consequence.h"
#include "model/consequence/consequence_clip_length.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_scan.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <array>
#include <span>

// `util/functions.h` is not reachable from this layer (`check_layer_rules.py`'s
// `scenes-includes-outside` row lists no `util/`), and this is the one free function in it the OLED
// page needs. Declared rather than included -- the spelling `menu/nodes/output_items.cpp:47` already
// uses for the same symbol.
char const* getOutputTypeName(OutputType outputType, int32_t channel);

// `Song`'s own gate on the re-sync `setClipLength` would otherwise do (`model/song/song.cpp:886`,
// read at `:939`). `ClipView::lengthenClip` lowers it across an undo of a previous shorten
// (`gui/views/clip_view.cpp:85-88`) and legacy reaches it with exactly this bare `extern`
// (`clip_view.cpp:72`); this layer may not include `model/song/song.cpp`'s definition site either
// way, and `nodes/settings.cpp:76` already spells a model global this way for the same reason.
extern bool allowResyncingDuringClipLengthChange;

namespace deluge::scenes::audio_clip {

using deluge::ui::grid::GridFrame;
using deluge::ui::oled::OledFrame;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

/// @brief `deluge::ui::shell::Action` -- the shell's navigation request. Spelled through this alias
///        so `::Action` (the legacy undo-log entry, `model/action/action.h`) can keep its own name
///        in the length machinery below without either one being ambiguous.
using ShellAction = deluge::ui::shell::Action;

namespace {

/// @brief One l10n string id, as `Action::popup_string` wants it. `marker_scene.cpp`'s `sid`.
constexpr uint16_t sid(deluge::l10n::String s) {
	return static_cast<uint16_t>(s);
}

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
	// **And nothing else.** `blink_on_` and `blink_accum_ms_` are deliberately NOT reset here: an
	// earlier cut did reset them, and legacy's `focusRegained` touches `blinkOn` on none of its
	// paths (`:79-88`, read line by line). It does not have to -- with both markers down nothing
	// reads the phase, and the one gesture that raises a marker restarts it, which is `padAction`'s
	// own `blinkOn = true; setTimer(UI_SPECIFIC, kSampleMarkerBlinkTime)` pair (`:553-554`,
	// `:563-564`) and is ported as `set_end_marker_visible`/`set_start_marker_visible`. Unobservable
	// either way; removed because this function's whole documentation is what it does not do, and an
	// added write inside that is the last place to leave one (fix round 1, Minor 2).
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
	switch (event.kind) {
	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		// One arm for both kinds, because legacy has one: `Buttons::buttonAction`
		// (`hid/buttons.cpp:134`) hands `X_ENC`, `Y_ENC`, `SELECT_ENC` and `TEMPO_ENC` to
		// `getCurrentUI()->buttonAction` through the same call the ordinary buttons take, and
		// `AudioClipView::buttonAction` claims two of them. `services::GlobalControls::handle`
		// accepts the same pair for the same reason (`services/global_controls.cpp:564-566`).
		return button_action(event, ctx);

	case UiEvent::Kind::Pad:
		return pad_action(event, ctx);

	case UiEvent::Kind::Encoder:
		switch (event.encoder) {
		case deluge::ui::EncoderRole::Select: {
			// `AudioClipView::selectEncoderAction` (`audio_clip_view.cpp:726-738`) returns void:
			// legacy's `Encoders` calls it on the current UI and there is no "not dealt with".
			// Applied here rather than parked for `after_render`, for `MarkerScene::handle`'s
			// reason: `Select` is delivered at `Shell::tick` step 8, after every `after_render`.
			Result r;
			r.handled = true;
			if (select_encoder_action(event.delta, ctx.shift)) {
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
			return r;
		}
		case deluge::ui::EncoderRole::Horizontal:
			return horizontal_encoder_action(event.delta, ctx);
		case deluge::ui::EncoderRole::Vertical:
			return vertical_encoder_action(event.delta, ctx);
		default:
			// The two gold knobs and the tempo encoder are `services::ModKnobs`'s and
			// `services::GlobalControls`'s; `AudioClipView` overrides none of their hooks.
			return Result::unhandled();
		}

	case UiEvent::Kind::AllReleased:
		break;
	}
	return Result::unhandled();
}

// -- The UI-mode word (`gui/ui/ui.cpp:400-461`), transcribed ------------------------------------

void AudioClipScene::enter_ui_mode(uint32_t mode) {
	// `enterUIMode` (`gui/ui/ui.cpp:454-461`).
	if (mode > kExclusiveUiModesMask) {
		ui_mode_ |= mode;
	}
	else {
		ui_mode_ = (ui_mode_ & ~kExclusiveUiModesMask) | mode;
	}
}

void AudioClipScene::exit_ui_mode(uint32_t mode) {
	// `exitUIMode` (`gui/ui/ui.cpp:443-452`).
	if (mode > kExclusiveUiModesMask) {
		ui_mode_ &= ~mode;
	}
	else if ((ui_mode_ & kExclusiveUiModesMask) == mode) {
		ui_mode_ &= ~kExclusiveUiModesMask;
	}
}

bool AudioClipScene::ui_mode_active(uint32_t mode) const {
	// `isUIModeActive` (`gui/ui/ui.cpp:400-408`).
	if (mode > kExclusiveUiModesMask) {
		return (ui_mode_ & mode) != 0u;
	}
	return (ui_mode_ & kExclusiveUiModesMask) == mode;
}

// -- Buttons ------------------------------------------------------------------------------------

bool AudioClipScene::deactivate_marker_if_necessary() {
	// `buttonAction`'s `deactivateMarkerIfNecessary` tail (`audio_clip_view.cpp:455-464`). The
	// `getCurrentUI() == this` guard on the timer unset (`:459`) has no counterpart -- a `Scene`
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
	// `buttonAction`'s `BACK` arm's body (`audio_clip_view.cpp:419-446`), minus `:419-421`'s
	// card-routine defer, which the shell answers with `InputLowering::set_sd_busy` rather than
	// by asking the scene (`shell/scene.h`'s `Action` warning).
	if (clip_ == nullptr || song_ == nullptr) {
		return 0;
	}
	::Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED); // `:424`
	// `:426-430`. The `ModelStack` is `menu::clip_cleared`'s, for the one reason that header gives:
	// `model/model_stack.h` is reachable from two files in this library and this is not one.
	menu::clip_cleared(*song_, *clip_, action, !FlashStorage::automationClear, true);
	// `:437-442`. Legacy's two popups, chosen by the same flag that chose the argument above.
	const uint16_t popup = FlashStorage::automationClear ? sid(deluge::l10n::String::STRING_FOR_SAMPLE_CLEARED)
	                                                     : sid(deluge::l10n::String::STRING_FOR_CLIP_CLEARED);
	set_end_marker_visible(false); // `:443`, and `:444`'s timer unset, which the setter is
	return popup;
}

Result AudioClipScene::button_action(const UiEvent& event, const UiContext& ctx) {
	// `AudioClipView::buttonAction` (`audio_clip_view.cpp:324-470`), arm for arm and in legacy's
	// own order, because the order is what decides which arm claims a button.
	Result r;
	const ButtonId b = event.button;
	const bool on = event.on;

	// ---- SESSION_VIEW (`:330-358`) ----
	if (b == ButtonId::Session) {
		r.handled = true; // every path of this arm reaches `:469`'s DEALT_WITH
		if (on) {
			if (ui_mode_ == kUiModeNone) { // `:332`
				ui_mode_ = kUiModeHoldingSongButton;
				// `:334`. Legacy measures in `AudioEngine::audioSampleTimer`; this layer has no
				// clock (`shell-no-clock`) and takes the shell's `now_ms`, exactly as
				// `services::GlobalControls`' SAVE/LOAD arms do.
				song_button_pressed_ms_ = ctx.now_ms;
				// `:335`'s `indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, true)` is
				// `LedFrame`'s and this scene declares no LEDs -- the same stance
				// `focus_regained` takes for `IndicatorLED::BACK`.
				// `:336`'s `uiNeedsRendering(this, 0, 0xFFFFFFFF)` is the SIDEBAR only; `RepaintAll`
				// is the only `Action` that reaches the grid at all (`shell/scene.h`'s table).
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
			return r;
		}
		if (!ui_mode_active(kUiModeHoldingSongButton)) { // `:340-342`
			return r;
		}
		exit_ui_mode(kUiModeHoldingSongButton);                                                      // `:346`
		if (static_cast<uint32_t>(ctx.now_ms - song_button_pressed_ms_) > timeline::kShortPressMs) { // `:347`
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});           // `:348`
			return r;
		}
		// `:353-356`: `uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC)` and
		// `ClipMinder::transitionToArrangerOrSession()`. **NOT PORTED**, and it is the whole point
		// of the short press: that function (`model/clip/clip_minder.cpp:28-39`) names
		// `arrangerView` and `sessionView`, two `scenes-no-root-ui` globals, and neither view is
		// ported (E5's). The latch above is ported because three of this scene's own arms branch
		// on it; the exit it guards is not this plan's.
		return r;
	}

	// ---- CLIP_VIEW (`:360-368`) ----
	if (b == ButtonId::Clip) {
		// `changeRootUI(&automationView)` (`:366`) -- `automationView` is a `scenes-no-root-ui`
		// global and there is no `AutomationScene`, so the body is **not ported**. The event is
		// still claimed, because legacy claims it: this arm falls through to `:469`'s DEALT_WITH,
		// so `Buttons::buttonAction`'s tail never runs (`hid/buttons.cpp:139-141`) and neither
		// `View::buttonAction` nor the tail has a `CLIP_VIEW` arm to run anyway. Claiming it keeps
		// the routing identical; the missing navigation is recorded here and in the class doc.
		r.handled = true;
		return r;
	}

	// ---- PLAY / RECORD / SHIFT (`:370-381`) ----
	if (b == ButtonId::Play || b == ButtonId::Record || b == ButtonId::Shift) {
		// `goto dontDeactivateMarker` -> `return ClipView::buttonAction(...)`, which for these three
		// resolves through `TimelineView::buttonAction`'s `else` (`timeline_view.cpp:119-121`) to
		// `view.buttonAction` -- `services::GlobalControls`. Unhandled is exactly that hand-off,
		// **and the marker is deliberately not deactivated**: that is what the `dontDeactivateMarker`
		// label buys, since these three return before the tail at `audio_clip_view.cpp:456`.
		return Result::unhandled();
	}

	// ---- X_ENC (`:383-397`) ----
	if (b == ButtonId::XEnc) {
		r.handled = true;
		if (ctx.held.has(ButtonId::YEnc)) {               // `:385`
			if (on && ui_mode_ == kUiModeNone) {          // `:386`
				set_clip_length_equal_to_sample_length(); // `:390`
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
			return r;
		}
		if (ctx.shift) {
			// `:394`'s `else if (!Buttons::isShiftButtonPressed())` is false, so the arm does
			// nothing and -- crucially -- never delegates. That is what leaves
			// `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` unset while shift+X_ENC is held, which is
			// what makes `horizontalEncoderAction`'s `isNoUIModeActive()` first arm reachable at all
			// (`:789`). See the class docstring.
			return r;
		}
		// `audio_clip_view.cpp:395`'s `goto dontDeactivateMarker` -> `ClipView::buttonAction`
		// (`clip_view.cpp:47-70`).
		if (on && !scrollable_and_zoomable()) {
			// `clip_view.cpp:52`: an empty block, then DEALT_WITH -- so a clip that cannot be
			// zoomed does not enter the mode at all.
			return r;
		}
		// `TimelineView::buttonAction`'s X_ENC arm (`gui/views/timeline_view.cpp:73-90`).
		if (on) {
			// `:76-78`'s `displayZoomLevel()` is **not ported**: its text comes from
			// `Song::getNoteLengthName` (`model/song/song.h:352`), whose only overload writes into
			// an `etl::istring&`, and `etl/` is outside this layer's include list
			// (`scenes-includes-outside`). It is `TimelineView`'s function, shared by every timeline
			// view, so the widget it wants belongs with whoever ports the next one.
			enter_ui_mode(kUiModeHoldingHorizontalEncoder); // `:80`
		}
		else if (ui_mode_active(kUiModeHoldingHorizontalEncoder)) {                 // `:85`
			deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelPopup); // `:86`
			exit_ui_mode(kUiModeHoldingHorizontalEncoder);                          // `:87`
		}
		return r;
	}

	// ---- SELECT_ENC without shift (`audio_clip_view.cpp:400-414`) ----
	if (b == ButtonId::SelectEnc && !ctx.shift) {
		// `soundEditor.setup(getCurrentClip())` + `openUI(&soundEditor)` (`audio_clip_view.cpp:406-409`).
		// `soundEditor` is a `scenes-no-root-ui` global; its port is `menu::MenuScene`, but nothing
		// in this library pushes that scene -- resolving a `MenuContext` for a clip and filing a
		// `Push` is the wiring plan's, exactly as `MarkerScene` records for its own shortcut jump
		// (`scenes/marker/marker_scene.h`). **Not ported**, and therefore not claimed: the event
		// falls through, where `services::GlobalControls` gates its own `SELECT_ENC` arm on shift
		// and does nothing with it either. The divergence is legacy's `goto
		// deactivateMarkerIfNecessary` at `audio_clip_view.cpp:412`, which puts the end marker down as the editor
		// opens; with no editor to open, putting it down would be a gesture with no cause.
		return Result::unhandled();
	}

	// ---- BACK while the horizontal encoder button is held (`:417-447`) ----
	// **The exact comparison is legacy's** (`:417`): with the song button also held, `ui_mode_` is
	// `kUiModeHoldingSongButton | kUiModeHoldingHorizontalEncoder` and this arm does not match, so
	// `BACK` goes back to being undo. See the class docstring for why this arm is load-bearing.
	if (b == ButtonId::Back && ui_mode_ == kUiModeHoldingHorizontalEncoder) {
		r.handled = true; // claimed on release too -- `:418`'s `if (on)` guards the body, not the arm
		if (on) {
			const uint16_t popup = clear_clip();
			if (popup != 0) {
				(void)r.actions.push_back(
				    ShellAction{.kind = ShellAction::Kind::Popup, .surface = SurfaceRole::Main, .popup_string = popup});
			}
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll}); // `:445`
		}
		return r;
	}

	// ---- everything else (`:448-467`) ----
	// `ClipMinder::buttonAction` (`model/clip/clip_minder.cpp:23-25`) is `return NOT_DEALT_WITH;`
	// and nothing else, so this reduces to `ClipView::buttonAction` -> (no override) ->
	// `TimelineView::buttonAction`, whose only remaining arm is TRIPLETS.
	if (b == ButtonId::Triplets && on) {
		// `timeline_view.cpp:93-99`. `setTripletsLEDState` (`timeline_view.cpp:365-372`) is
		// `LedFrame`'s and this scene declares no LEDs.
		timeline_.toggle_triplets();
		r.handled = true;
	}
	// `audio_clip_view.cpp:455-464`, and it runs whether or not the arm above claimed the button:
	// legacy's tail is outside the `if`, guarded only on the result not being
	// `REMIND_ME_OUTSIDE_CARD_ROUTINE`. Read against the enclosing braces rather than the shape:
	// the `if (result != ...)` at `:455` closes at `:464`, inside the `else` block that opened at
	// `:448` and closes at `:467`.
	if (deactivate_marker_if_necessary()) {
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll}); // `:462`
	}
	return r;
}

// -- Pads ---------------------------------------------------------------------------------------

Result AudioClipScene::pad_action(const UiEvent& event, const UiContext& ctx) {
	// `AudioClipView::padAction` (`audio_clip_view.cpp:472-589`). Every path of it returns
	// DEALT_WITH (`:588`, and the sidebar arm's own `:585`), so this claims every pad it is given.
	Result r;
	r.handled = true;
	const int32_t x = event.x;
	const bool on = event.on;

	if (x >= kMainCols) {
		// `:572-586`: the first sidebar column, and only while the song button is held.
		if (x == kMainCols && ui_mode_active(kUiModeHoldingSongButton)) {
			if (on) {
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll}); // `:579`
			}
			// `:583`'s `view.activateMacro(y)` is **not ported**, for the reason
			// `renderSidebar`'s macro arm is not (see the class docstring's `@warning`): the macros
			// live on `View`, whose port is `services::GlobalControls`, and this scene holds no
			// handle to it. Nothing draws the macros here either, so the gesture has nothing to act
			// on and nothing to show.
		}
		return r;
	}

	if (ctx.held.has(ButtonId::TempoEnc)) {           // `:474`
		if (on && clip_ != nullptr) {                 // `:475`
			playbackHandler.grabTempoFromClip(clip_); // `:476`
		}
		return r;
	}

	// `:480-482`'s `isSDRoutineActive()` defer has no counterpart: the shell's SD gate is
	// `InputLowering::set_sd_busy`, which defers and retries rather than asking a scene
	// (`shell/scene.h`'s `Action` warning names that split).
	//
	// `:485`'s `soundEditor.potentialShortcutPadAction(x, y, on)` is **not ported**, for
	// `MarkerScene`'s reason (`scenes/marker/marker_scene.h`): `MenuScene` owns the shortcut
	// tables, and jumping into a menu item from here means filing navigation nothing yet consumes.
	// The consequence is behavioural and worth stating plainly: on a pad that IS a shortcut, legacy
	// opens the editor and puts BOTH markers down (`:487-492`), while this port runs the marker
	// gesture below instead.

	if (!on || !no_ui_mode_active()) { // `:495`
		return r;
	}
	if (clip_ == nullptr) { // `:497-499`
		return r;
	}

	const int32_t zoom = static_cast<int32_t>(timeline_.x_zoom == 0 ? 1 : timeline_.x_zoom);
	// `:502-507`. Both squares come from the scene's own scroll and zoom, which `open()` adopted
	// from the same `Song` slot legacy reads.
	const int32_t end_square = timeline::divide_round_negative(clip_->loopLength - timeline_.x_scroll - 1, zoom);
	const int32_t start_square = timeline::divide_round_negative(0 - timeline_.x_scroll, zoom);

	const auto repaint = [&r] { (void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll}); };

	if (end_marker_visible_) { // `:510`
		if (x == end_square || x == start_square) {
			// `:512-516`: tapping either edge column puts the marker back down.
			set_end_marker_visible(false);
			repaint();
			return r;
		}
		::Sample* const sample = current_sample(); // `:518`
		if (sample != nullptr) {
			const int32_t new_length = (x + 1) * zoom + timeline_.x_scroll; // `:520-521`
			const int32_t old_length = clip_->loopLength;                   // `:522`
			const auto old_length_samples = static_cast<uint64_t>(clip_->sampleHolder.getDurationInSamples(true));
			change_underlying_sample_length(*sample, new_length, old_length, old_length_samples); // `:524`
			repaint();                                                                            // `:525`
		}
		return r;
	}

	if (start_marker_visible_) { // `:530`
		if (x == start_square || x == end_square) {
			// `:531-535`.
			set_start_marker_visible(false);
			repaint();
			return r;
		}
		::Sample* const sample = current_sample(); // `:537`
		if (sample != nullptr) {
			const int32_t new_start_ticks = x * zoom + timeline_.x_scroll; // `:539-540`
			const int32_t old_length = clip_->loopLength;                  // `:541`
			const auto old_length_samples = static_cast<uint64_t>(clip_->sampleHolder.getDurationInSamples(true));
			change_underlying_sample_start(*sample, new_start_ticks, old_length, old_length_samples); // `:543`
			repaint();                                                                                // `:544`
		}
		return r;
	}

	// `:548-568`: no marker up. Are we on (or just past) the end column, or on the start column?
	if (x == end_square || x == end_square + 1) {
		// `:551-555`. `set_end_marker_visible(true)` is `endMarkerVisible = true` plus `:553-554`'s
		// `blinkOn = true` and the timer arm; `startMarkerVisible` is already false in this branch,
		// so the setter below is a no-op that says so.
		set_end_marker_visible(true);
		set_start_marker_visible(false);
		repaint();
	}
	else if (x == start_square
	         && runtimeFeatureSettings.get(RuntimeFeatureSettingType::TrimFromStartOfAudioClip)
	                == RuntimeFeatureStateToggle::On) {
		// `:557-566`. Legacy's own "WIP" arm, behind a runtime feature toggle that is off by
		// default; the port reads the same global.
		set_start_marker_visible(true);
		set_end_marker_visible(false);
		repaint();
	}
	return r;
}

// -- Encoders -----------------------------------------------------------------------------------

bool AudioClipScene::select_encoder_action(int32_t offset, bool shift) {
	// `AudioClipView::selectEncoderAction` (`audio_clip_view.cpp:726-738`).
	if (!no_ui_mode_active()) { // `:727-729`
		return false;
	}
	if (shift) {
		// `:732`'s `view.navigateThroughAudioOutputsForAudioClip` is **not ported**. Two of its
		// three halves are outside this layer: it builds a `ModelStack` for `Clip::changeOutput`
		// and `Output::pickAnActiveClipIfPossible` (`gui/views/view.cpp:2205-2211`), and it ends in
		// `setActiveModControllableTimelineCounter` (`:2215`), which is `services::ModKnobs`' --
		// the same handle `renderSidebar`'s macro arm lacks. It is a `View` method shared with the
		// session view, so it belongs with `services::ModKnobs`/E5 rather than being rebuilt here.
		return false;
	}
	// `audio_clip_view.cpp:735-736` (spelled with the file, because the paragraph above cites
	// `view.cpp` line numbers). `AudioClip::output` is always an `AudioOutput` -- `setOutput` is
	// only ever handed one -- which is the same cast legacy makes.
	if (clip_ == nullptr || clip_->output == nullptr) {
		return false;
	}
	static_cast<AudioOutput*>(clip_->output)->scrollAudioOutputMode(static_cast<int8_t>(offset));
	return true;
}

Result AudioClipScene::vertical_encoder_action(int32_t offset, const UiContext& ctx) {
	// `AudioClipView::verticalEncoderAction` (`audio_clip_view.cpp:837-848`).
	Result r;
	r.handled = true; // `:847`: DEALT_WITH on every path, claimed or not
	if (no_ui_mode_active() && ctx.shift && !ctx.held.has(ButtonId::YEnc)) { // `:838`
		// `:839-841`'s card-routine defer has no counterpart here (see `pad_action`).
		if (clip_ != nullptr) {
			clip_->colourOffset += static_cast<int16_t>(offset);                           // `:845`
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll}); // `:846`
		}
	}
	return r;
}

Result AudioClipScene::horizontal_encoder_action(int32_t offset, const UiContext& ctx) {
	// `AudioClipView::horizontalEncoderAction` (`audio_clip_view.cpp:787-796`).
	if (no_ui_mode_active() && ctx.held.has(ButtonId::XEnc) && ctx.shift) { // `:789`
		return edit_clip_length_without_timestretching(offset);             // `:790`
	}
	return clip_horizontal_encoder_action(offset, ctx); // `:794`
}

Result AudioClipScene::clip_horizontal_encoder_action(int32_t offset, const UiContext& ctx) {
	// `ClipView::horizontalEncoderAction` (`gui/views/clip_view.cpp:146-263`), three arms.
	Result r;
	r.handled = true; // every arm ends in DEALT_WITH

	// ---- shift: edit the length, WITH timestretching (`clip_view.cpp:149-181`) ----
	if (no_ui_mode_active() && !ctx.held.has(ButtonId::YEnc) && ctx.shift) {
		if (!scrollable_and_zoomable()) { // `:153`
			(void)r.actions.push_back(
			    ShellAction{.kind = ShellAction::Kind::Popup,
			                .surface = SurfaceRole::Main,
			                .popup_string = sid(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH)});
			return r; // `:154-155`
		}
		const auto old_length = static_cast<uint32_t>(clip_->loopLength);                             // `:158`
		if (timeline_.scroll_right_to_end_of_length_if_necessary(static_cast<int32_t>(old_length))) { // `:161`
			(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			return r; // `:162`
		}
		// `:167-169`'s SD defer: none, as above.
		::Action* action = nullptr;                                                 // `:171`
		const uint32_t new_length = change_clip_length(offset, old_length, action); // `:173`
		show_bars_and_beats(new_length, /*count_from_one=*/false);                  // `:175`
		if (action != nullptr) {
			action->xScrollClip[AFTER] = timeline_.x_scroll; // `:178`
		}
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
		return r;
	}

	// ---- Y_ENC (or X_ENC held + CLIP): shift the clip's contents (`clip_view.cpp:184-250`) ----
	if ((no_ui_mode_active() && ctx.held.has(ButtonId::YEnc))
	    || (ui_mode_ == kUiModeHoldingHorizontalEncoder && ctx.held.has(ButtonId::Clip))) {
		// **Not ported.** `Clip::shiftHorizontally` (`:208`) is model, but two of the three
		// arguments legacy passes are decided by `getCurrentUI() == &automationView`
		// (`clip_view.cpp:203-206`) -- a `scenes-no-root-ui` read this layer may not make and, with
		// no `AutomationScene`, a question with no answer. Its undo consequence also
		// `alloc_sdram`s a `ConsequenceClipHorizontalShift` (`:240-245`), which plan constraint 6
		// forbids from library code. The gesture is still **claimed**, because legacy claims it
		// (`:249`): letting it fall through would hand a Y_ENC turn to `GlobalControls`, which
		// legacy never does.
		//
		// **It is therefore a silent no-op for a real gesture** -- on the device this shifts the
		// clip's contents -- and it must NOT survive into E5 in that state: an `AutomationScene`
		// answers the `getCurrentUI() == &automationView` question, and once one exists this arm
		// has no excuse left.
		return r;
	}

	// ---- otherwise, scroll and zoom (`clip_view.cpp:253-262`) ----
	if (!scrollable_and_zoomable()) { // `:256-258`
		return r;
	}
	return nav_horizontal_encoder_action(offset, ctx); // `:261`
}

Result AudioClipScene::nav_horizontal_encoder_action(int32_t offset, const UiContext& ctx) {
	// `ClipNavigationTimelineView::horizontalEncoderAction`
	// (`gui/views/clip_navigation_timeline_view.cpp:30-34`).
	//
	// **First statement, above every gate below it** -- `:32` -- which is the whole reason
	// `TimelineState::stop_following()` is public. `zoom()` and `scroll()` call it too, so on a turn
	// that moves something this is redundant; on a turn the gates below refuse it is the only call,
	// and legacy clears there just the same. See the class docstring for why it is here and not in
	// `horizontal_encoder_action` above.
	timeline_.stop_following();

	Result r;
	r.handled = true; // `timeline_view.cpp:227`: DEALT_WITH on every path that is not a defer

	// `timeline_view.cpp:137-151`'s three defers -- `isSDRoutineActive()`, `pendingUIRenderingLock`
	// and `horizontalEncoderActionLock` -- have no counterpart: the first is
	// `InputLowering::set_sd_busy`'s, and the other two guard a re-entrancy legacy's own comment at
	// `:141-142` doubts, which an immediate-mode scene called once per event cannot have.
	//
	// `:158`'s `inNoteRowEditor` is `getCurrentUI() == &soundEditor` -- a `scenes-no-root-ui` read,
	// and false for this view in legacy too unless the sound editor is open over it. Constant false
	// here, which is what makes both `|| inNoteRowEditor` disjuncts below drop out.

	if (ui_mode_active(kUiModeHoldingHorizontalEncoder)) { // `:161`
		if (ui_mode_ == kUiModeHoldingHorizontalEncoder) { // `:163`, exclusively
			// `:165-198`, whose state half is `TimelineState::zoom` (E4 task 8) -- the max-zoom
			// delay, the zoom limits and the scroll realignment included.
			const timeline::ZoomOutcome outcome = timeline_.zoom(offset, max_zoom(), ctx.now_ms);
			if (outcome == timeline::ZoomOutcome::Zoomed) {
				// `:198`'s `displayZoomLevel()` is not ported; see `button_action`'s X_ENC arm for
				// the reason. The repaint is.
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
		}
		return r;
	}

	if (no_ui_mode_active()) { // `:203`, minus `UI_MODE_AUDITIONING`, which this scene cannot enter
		if (!ctx.shift) {      // `:206`
			const bool moved = timeline_.scroll(offset, max_length()); // `:208-220`
			if (moved) {
				(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
			}
			// `:221`'s `displayScrollPos()` sits OUTSIDE the `if (newXScroll != ...)` at `:217`, so
			// legacy shows the position even on a turn that moved nothing. Read from the enclosing
			// braces, not from the shape: `:216`'s `if` opens and closes at `:220`, `:217`'s inner
			// `if` closes at `:219`, and `:221` sits after both, inside the `if (!shift)` block that
			// opened at `:206` and closes at `:222`.
			show_bars_and_beats(static_cast<uint32_t>(timeline_.x_scroll), /*count_from_one=*/true);
		}
	}
	return r;
}

// -- The two edits, and the length machinery they share ------------------------------------------
//
// `Song::setClipLength` is the one write both edits end in; everything around it is the undo
// transaction legacy runs so a revert can put both the length AND the marker back.

void AudioClipScene::change_underlying_sample_length(const ::Sample& sample, int32_t new_length, int32_t old_length,
                                                     uint64_t old_length_samples) {
	// `AudioClipView::changeUnderlyingSampleLength` (`audio_clip_view.cpp:592-646`).
	if (clip_ == nullptr || song_ == nullptr || old_length == 0) {
		return;
	}
	uint64_t* value_to_change = nullptr;
	int64_t new_end_pos_samples = 0;

	// `:597-598`. Legacy's own widths: the multiply is done in `uint64_t` and the divide by a
	// `uint32_t`, so a negative `oldLength` would be a different expression -- it cannot be one
	// here (`loopLength` is positive on every path that reaches this).
	const uint64_t new_length_samples =
	    static_cast<uint64_t>(old_length_samples * static_cast<uint64_t>(new_length) + (old_length >> 1))
	    / static_cast<uint32_t>(old_length);

	if (clip_->sampleControls.isCurrentlyReversed()) { // `:601`
		new_end_pos_samples =
		    static_cast<int64_t>(clip_->sampleHolder.endPos) - static_cast<int64_t>(new_length_samples);
		if (new_end_pos_samples < 0) { // `:603-605`
			new_end_pos_samples = 0;
		}
		value_to_change = &clip_->sampleHolder.startPos; // `:606`
	}
	else {
		// `:610`. Playing forwards, the END pos is what moves.
		new_end_pos_samples =
		    static_cast<int64_t>(clip_->sampleHolder.startPos) + static_cast<int64_t>(new_length_samples);
		if (new_end_pos_samples > static_cast<int64_t>(sample.lengthInSamples)) { // `:611-613`
			new_end_pos_samples = static_cast<int64_t>(sample.lengthInSamples);
		}
		value_to_change = &clip_->sampleHolder.endPos; // `:614`

		// `:617-625`: snap to the loop-end marker written in the audio file, if we landed within
		// ten samples of it.
		if (sample.fileLoopStartSamples != 0) {
			int64_t distance = new_end_pos_samples - static_cast<int64_t>(sample.fileLoopStartSamples);
			if (distance < 0) {
				distance = -distance;
			}
			if (distance < 10) {
				new_end_pos_samples = static_cast<int64_t>(sample.fileLoopStartSamples);
			}
		}
	}

	const ActionType action_type = (new_length < old_length) ? ActionType::CLIP_LENGTH_DECREASE // `:628-629`
	                                                         : ActionType::CLIP_LENGTH_INCREASE;

	// `:631-634`. The marker must move BEFORE `setClipLength`, which reads it -- legacy's own
	// comment says so, and the order is load-bearing rather than stylistic.
	const uint64_t old_value = *value_to_change;
	*value_to_change = static_cast<uint64_t>(new_end_pos_samples);

	::Action* action = actionLogger.getNewAction(action_type, ActionAddition::NOT_ALLOWED); // `:636`
	song_->setClipLength(clip_, static_cast<uint32_t>(new_length), action);                 // `:637`
	if (action != nullptr) {                                                                // `:638-645`
		if (action->firstConsequence != nullptr && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
			auto* consequence = static_cast<ConsequenceClipLength*>(action->firstConsequence);
			consequence->pointerToMarkerValue = value_to_change;
			consequence->markerValueToRevertTo = old_value;
		}
		actionLogger.closeAction(action_type);
	}
}

void AudioClipScene::change_underlying_sample_start(const ::Sample& sample, int32_t new_start_ticks, int32_t old_length,
                                                    uint64_t old_length_samples) {
	// `AudioClipView::changeUnderlyingSampleStart` (`audio_clip_view.cpp:649-701`).
	if (clip_ == nullptr || song_ == nullptr || old_length == 0) {
		return;
	}
	const int32_t old_end_tick = old_length;                   // `:651`
	int32_t new_length_ticks = old_end_tick - new_start_ticks; // `:652`
	if (new_length_ticks < 1) {                                // `:653-655`
		new_length_ticks = 1;
	}
	const uint64_t new_length_samples =
	    static_cast<uint64_t>(old_length_samples * static_cast<uint64_t>(new_length_ticks) + (old_length / 2))
	    / static_cast<uint32_t>(old_length); // `:656-657`

	// Both arms are the same five steps over a different field, exactly as legacy writes them
	// twice. The comparison inside `actionType` is `newLengthTicks < oldLength` on BOTH arms
	// (`:667-668`, `:688-689`) -- not `newStartTicks`, and not the reversed arm's own value.
	uint64_t* value_to_change = nullptr;
	uint64_t old_value = 0;
	if (clip_->sampleControls.isCurrentlyReversed()) {                            // `:659`
		old_value = clip_->sampleHolder.endPos;                                   // `:660`
		uint64_t new_end_pos = clip_->sampleHolder.startPos + new_length_samples; // `:661`
		if (new_end_pos > sample.lengthInSamples) {                               // `:662-664`
			new_end_pos = sample.lengthInSamples;
		}
		clip_->sampleHolder.endPos = new_end_pos; // `:665`
		value_to_change = &clip_->sampleHolder.endPos;
	}
	else {
		old_value = clip_->sampleHolder.startPos;                                 // `:681`
		uint64_t new_start_pos = clip_->sampleHolder.endPos - new_length_samples; // `:682`
		if (static_cast<int64_t>(new_start_pos) < 0) {                            // `:683-685`
			new_start_pos = 0;
		}
		clip_->sampleHolder.startPos = new_start_pos; // `:686`
		value_to_change = &clip_->sampleHolder.startPos;
	}

	const ActionType action_type =
	    (new_length_ticks < old_length) ? ActionType::CLIP_LENGTH_DECREASE : ActionType::CLIP_LENGTH_INCREASE;
	::Action* action = actionLogger.getNewAction(action_type, ActionAddition::NOT_ALLOWED);
	song_->setClipLength(clip_, static_cast<uint32_t>(new_length_ticks), action);
	if (action != nullptr) {
		if (action->firstConsequence != nullptr && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
			auto* consequence = static_cast<ConsequenceClipLength*>(action->firstConsequence);
			consequence->pointerToMarkerValue = value_to_change;
			consequence->markerValueToRevertTo = old_value;
		}
		actionLogger.closeAction(action_type);
	}
}

::Action* AudioClipScene::lengthen_clip(int32_t new_length) {
	// `ClipView::lengthenClip` (`gui/views/clip_view.cpp:75-121`).
	::Action* action = nullptr;
	::Action* const first = actionLogger.firstAction[BEFORE];

	// `:80-82`: if the last action was a shorten of this very clip, undo it instead of adding a
	// second consequence on top.
	const bool undoing = first != nullptr && first->openForAdditions && first->type == ActionType::CLIP_LENGTH_DECREASE
	                     && first->currentClip == clip_;
	if (undoing) {
		// `:84-89`. `allowResyncingDuringClipLengthChange` lives in `model/song/song.cpp:886`;
		// legacy's `clip_view.cpp:72` reaches it with a bare `extern` and so does this file, for the
		// reason `nodes/settings.cpp:76` reaches `jcong` that way -- the layer rules forbid the
		// header, not the symbol.
		allowResyncingDuringClipLengthChange = false;
		(void)actionLogger.revert(BEFORE, false, false);
		allowResyncingDuringClipLengthChange = true;
	}

	if (clip_->loopLength != new_length) {                                                           // `:93`
		ActionType action_type = (new_length < clip_->loopLength) ? ActionType::CLIP_LENGTH_DECREASE // `:94-95`
		                                                          : ActionType::CLIP_LENGTH_INCREASE;
		// `:98-100`. **Legacy dereferences `actionLogger.firstAction[BEFORE]` at `:98` without a
		// null check**, and again at `shortenClip`'s `:129`. The nearest null-guarded read of the
		// same pointer is the `undoing` test at `:80-82` -- **eighteen lines up, in a different
		// `if`, whose result is not carried down here** -- and `shortenClip` has no earlier test at
		// all, so this is easy to miss rather than an obvious oversight. It survives on the device
		// because an `Action` almost always exists by the time a length edit runs; the window is a
		// fresh boot, or an `actionLogger.deleteAllLogs()` (which `SoundEditor::setup` does on every
		// menu open, `gui/ui/sound_editor.cpp:1716`) followed immediately by a length edit. The
		// port reads null as "not a PATTERN_PASTE", which is the answer legacy computes for every
		// non-null value it has ever actually seen there. Recorded in `src/scenes/README.md` beside
		// the `getTickSquare` `xZoom`-for-`xScroll` defect; unlike that one it is NOT reproduced,
		// because a port cannot faithfully reproduce a segfault.
		::Action* const now_first = actionLogger.firstAction[BEFORE];
		if (now_first != nullptr && now_first->type == ActionType::PATTERN_PASTE) {
			action_type = ActionType::PATTERN_PASTE;
		}

		action = actionLogger.getNewAction(action_type, ActionAddition::ALLOWED); // `:102`
		if (action != nullptr && action->currentClip != clip_) {                  // `:103-105`
			action = actionLogger.getNewAction(action_type, ActionAddition::NOT_ALLOWED);
		}
		song_->setClipLength(clip_, static_cast<uint32_t>(new_length), action); // `:107`
	}
	else if (undoing && playbackHandler.isEitherClockActive()) {
		// `:111-118`: the revert above already produced the right length, so the re-sync it would
		// have done has to be done by hand. The `ModelStack` is `menu::clip_resynced`'s, for the
		// reason `clip_cleared` is (`scenes/menu/context.h`).
		menu::clip_resynced(*song_, *clip_);
	}
	return action;
}

::Action* AudioClipScene::shorten_clip(int32_t new_length) {
	// `ClipView::shortenClip` (`gui/views/clip_view.cpp:124-144`).
	::Action* action = nullptr;
	::Action* const first = actionLogger.firstAction[BEFORE];
	if (first != nullptr && first->type == ActionType::PATTERN_PASTE) { // `:129` -- unguarded in legacy
		action = actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
	}
	else {
		action = actionLogger.getNewAction(ActionType::CLIP_LENGTH_DECREASE, ActionAddition::ALLOWED); // `:133`
		if (action != nullptr && action->currentClip != clip_) {                                       // `:134-136`
			action = actionLogger.getNewAction(ActionType::CLIP_LENGTH_DECREASE, ActionAddition::NOT_ALLOWED);
		}
	}
	// `:139-142`. Legacy's comment: shortening again by more squares adds no second consequence,
	// because `setClipLength` records the snapshot and the original length only once.
	song_->setClipLength(clip_, static_cast<uint32_t>(new_length), action);
	return action;
}

uint32_t AudioClipScene::change_clip_length(int32_t offset, uint32_t old_length, ::Action*& action) {
	// `ClipView::changeClipLength` (`gui/views/clip_view.cpp:264-313`).
	bool right_on_square = false;
	uint32_t new_length = 0;
	const int32_t end_square = timeline_.square_from_pos(static_cast<int32_t>(old_length), &right_on_square); // `:267`

	if (offset > 0) { // `:270`
		new_length = static_cast<uint32_t>(timeline_.pos_from_square(end_square)
		                                   + timeline_.length_extend_amount(end_square)); // `:272`
		if (new_length <= static_cast<uint32_t>(kMaxSequenceLength)) {                    // `:275`
			action = lengthen_clip(static_cast<int32_t>(new_length));                     // `:277`
			// `:279-281`: the scroll happens either way; only the redraw is conditional, and the
			// redraw is the caller's `RepaintAll`.
			(void)timeline_.scroll_right_to_end_of_length_if_necessary(static_cast<int32_t>(new_length));
		}
	}
	else {
		// `:289-294`. Landing exactly on a column edge chops the previous square's width; landing
		// inside one snaps back to that column's start.
		new_length = right_on_square ? old_length - static_cast<uint32_t>(timeline_.length_chop_amount(end_square))
		                             : static_cast<uint32_t>(timeline_.pos_from_square(end_square));
		if (new_length > 0) {                                                                // `:296`
			action = shorten_clip(static_cast<int32_t>(new_length));                         // `:298`
			if (!timeline_.scroll_left_if_too_far_right(static_cast<int32_t>(new_length))) { // `:301`
				(void)timeline_.zoom_to_max(max_zoom(), /*in_only=*/true);                   // `:303`
			}
		}
	}
	return new_length;
}

Result AudioClipScene::edit_clip_length_without_timestretching(int32_t offset) {
	// `AudioClipView::editClipLengthWithoutTimestretching` (`audio_clip_view.cpp:798-835`).
	//
	// A DISTINCT arm from `adjust_loop_length`: this one moves the sample's own end marker with the
	// clip (`:826`), so the audio keeps its pitch and speed and gets longer or shorter; the other
	// moves only the clip and leaves the timestretch to absorb the difference.
	Result r;
	r.handled = true;
	if (clip_ == nullptr || song_ == nullptr) {
		return r;
	}
	if (!scrollable_and_zoomable()) { // `:800`
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::Popup,
		                                      .surface = SurfaceRole::Main,
		                                      .popup_string = sid(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH)});
		return r; // `:802`
	}
	if (timeline_.scroll_right_to_end_of_length_if_necessary(clip_->loopLength)) { // `:806`
		(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
		return r; // `:807`
	}
	// `:810-812`'s SD defer: none, as everywhere else in this file.

	const int32_t old_length = clip_->loopLength;                                                          // `:814`
	const auto old_length_samples = static_cast<uint64_t>(clip_->sampleHolder.getDurationInSamples(true)); // `:815`

	::Action* action = nullptr;                                                                        // `:817`
	const uint32_t new_length = change_clip_length(offset, static_cast<uint32_t>(old_length), action); // `:818`

	// `:820-828`. Legacy reads the sample through the playback GUIDE's holder here, not through
	// `getSample()` -- so while a linear recording is running this is the clip's own file rather
	// than the recorder's. Transcribed rather than tidied.
	auto* const holder = static_cast<SampleHolder*>(clip_->guide.audioFileHolder);
	if (holder != nullptr) {
		auto* const sample = static_cast<::Sample*>(holder->audioFile);
		if (sample != nullptr) {
			change_underlying_sample_length(*sample, static_cast<int32_t>(new_length), old_length, old_length_samples);
		}
	}

	show_bars_and_beats(new_length, /*count_from_one=*/false); // `:830`
	if (action != nullptr) {                                   // `:831-833`
		action->xScrollClip[AFTER] = timeline_.x_scroll;
	}
	(void)r.actions.push_back(ShellAction{.kind = ShellAction::Kind::RepaintAll});
	return r;
}

void AudioClipScene::adjust_loop_length(int32_t new_length) {
	// `AudioClipView::adjustLoopLength` (`audio_clip_view.cpp:753-785`).
	if (clip_ == nullptr || song_ == nullptr) {
		return;
	}
	const int32_t old_length = clip_->loopLength; // `:754`
	if (old_length == new_length) {               // `:756` -- the whole body is inside this test
		return;
	}

	::Action* action = nullptr; // `:757`
	if (new_length > old_length) {
		if (new_length <= static_cast<int32_t>(kMaxSequenceLength)) { // `:761`
			action = lengthen_clip(new_length);                       // `:762`
			// `:763-765`'s `doReRender` label: the redraw, which the caller's `RepaintAll` is.
		}
	}
	else if (new_length < old_length) {        // `:768`
		if (new_length > 0) {                  // `:769`
			action = shorten_clip(new_length); // `:770`
			// `:772-776`: scroll left if the view is now past the end; failing that, zoom in to the
			// clip's new max. Only the `else` of the inner test reaches `doReRender`, and the
			// redraw is the caller's either way.
			if (!timeline_.scroll_left_if_too_far_right(new_length)) {
				(void)timeline_.zoom_to_max(max_zoom(), /*in_only=*/true);
			}
		}
	}

	show_bars_and_beats(static_cast<uint32_t>(new_length), /*count_from_one=*/false); // `:780`
	if (action != nullptr) {                                                          // `:781-783`
		action->xScrollClip[AFTER] = timeline_.x_scroll;
	}
}

void AudioClipScene::set_clip_length_equal_to_sample_length() {
	// `AudioClipView::setClipLengthEqualToSampleLength` (`audio_clip_view.cpp:740-751`).
	//
	// `MenuAction::SetClipLengthToSampleLength`'s callee. Nothing dispatches that enumerator yet --
	// `scenes/menu/action.h` says so and says whose job it is -- so this method's only caller in
	// this library is `button_action`'s X_ENC+Y_ENC chord.
	if (clip_ == nullptr) {
		return;
	}
	// `:741-743`. The holder comes from the playback guide, not from `sampleHolder` directly: they
	// are the same object on a settled clip, and legacy reads the guide.
	auto* const holder = static_cast<SampleHolder*>(clip_->guide.audioFileHolder);
	if (holder == nullptr) {
		// `:749`.
		deluge::notify::notice_string(sid(deluge::l10n::String::STRING_FOR_NO_SAMPLE));
		return;
	}
	adjust_loop_length(holder->getLoopLengthAtSystemSampleRate(true));                         // `:745`
	deluge::notify::notice_string(sid(deluge::l10n::String::STRING_FOR_CLIP_LENGTH_ADJUSTED)); // `:746`
}

void AudioClipScene::show_bars_and_beats(uint32_t number, bool count_from_one) {
	// `TimelineView::displayNumberOfBarsAndBeats`' popup half (`gui/views/timeline_view.cpp:241-265`).
	// The decomposition is `TimelineState::bars_and_beats`, which took it in E4 task 8 and records
	// there that legacy's `quantization` and `tooLongText` parameters are read by nothing.
	if (song_ == nullptr) {
		return;
	}
	const uint32_t one_bar = song_->getBarLength();
	if (one_bar == 0) {
		return;
	}
	const timeline::BarsBeats b = timeline::TimelineState::bars_and_beats(number, one_bar, count_from_one);

	// `:262-263`'s `sprintf(buffer, "%d : %d : %d", ...)` then `popupTextTemporary`. Formatted by
	// hand for `render_oled`'s reason -- `deluge::string::fromInt` is in `util/`, which this layer
	// may not include -- and raised through the notify seam because it is computed text rather than
	// an l10n id (`scenes/slicing/slicer_scene.cpp` makes the same split).
	std::array<char, 15> buffer{}; // legacy's own `char buffer[15]` (`:262`)
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
	deluge::notify::notice_text(std::string_view{buffer.data(), len});
}

// ---------------------------------------------------------------------------------------------
// Model notifications

void AudioClipScene::on_model_changed(const deluge::notify::DirtySet& dirty) {
	// `clipNeedsReRendering` (`audio_clip_view.cpp:707-718`), `sampleNeedsReRendering` (`:720-724`)
	// and `playbackEnded` (`:703-705`). Two of the three are pure `uiNeedsRendering` calls, which
	// `Shell::apply_dirty` (`shell/shell.cpp:502-527`) already does for these facts; what is left is
	// the scroll-back-left legacy hides inside `clipNeedsReRendering`.
	if (!dirty.has(deluge::notify::Dirty::ClipContent) || clip_ == nullptr || song_ == nullptr) {
		return;
	}
	// `:708`'s `if (c == getCurrentAudioClip())`. The fact carries clip bits when the raiser named
	// one; a raise with no index means "which clips is unknown -- treat as all dirty"
	// (`notify/dirty_set.h`'s `has()` note), which is the over-invalidating side and so is safe to
	// take as "this clip".
	const deluge::notify::ClipBits& clips = dirty.clips(deluge::notify::Dirty::ClipContent);
	if (clips.any()) {
		const int32_t index = song_->sessionClips.getIndexForClip(clip_);
		if (index < 0 || !clips.test(static_cast<uint16_t>(index))) {
			return;
		}
	}
	// `:711-713`. Legacy's own comment: "it seems it was a bit hacky that I put this in this
	// function". Ported where legacy has it, hack and all.
	if (timeline_.x_scroll >= clip_->loopLength) {
		(void)timeline_.scroll_for_linear_recording(0, max_length());
	}
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
