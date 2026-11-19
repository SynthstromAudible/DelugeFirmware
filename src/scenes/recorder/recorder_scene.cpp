/// @file
/// @brief `RecorderScene`. See `recorder_scene.h`.

#include "scenes/recorder/recorder_scene.h"

#include "ui/oled/frame.h"
#include "ui/oled/text.h"

#include "notify/notice_queue.h"

#include "scenes/bind/target.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/shortcut_blink.h"
#include "ui/grid/frame.h"

#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "gui/l10n/strings.h"
#include "model/action/action_logger.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "processing/recording/foreground_recording.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace deluge::scenes::recorder {

using deluge::l10n::String;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// The record dot's half-period while recording, and the threshold line's cycle while waiting, lit for its first 290 ms
/// and from 570 ms on (`AudioRecorder::getThresholdMeterState` (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`)).
/// @{
constexpr uint32_t kRecordDotHalfPeriodMs = 480;
constexpr uint32_t kThresholdBlinkCycleMs = 750;
constexpr uint32_t kThresholdLitUntilMs = 290;
constexpr uint32_t kThresholdLitFromMs = 570;
/// @}

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

/// The two words `AudioRecorder::renderOLED` draws. Literals, not l10n ids, because
/// neither is in `gui/l10n/strings.h` -- exactly `BrowserScene::Descriptor::title`'s and
/// `MarkerScene`'s situation and answer.
/// @{
constexpr std::string_view kWaiting = "Waiting";
constexpr std::string_view kRecording = "Recording";
/// @}

/// `display->displayLoadingAnimationText("Working")`, raised the way `browser::Listing`
/// raises the same thing: `Notices::drain` turns a `Working` notice into `SceneKind::Working` over
/// this scene (`shell/notices.cpp`), and `ClearWorking` takes it away again.
constexpr std::string_view kWorking = "Working";

/// `opened()`'s `display->displayPopup("Loading")`: a literal, as legacy's is.
constexpr std::string_view kLoading = "Loading";

/// How many numbers `name_drum` tries after "REC" before it gives up: more than a kit has rows.
constexpr int32_t kMaxRecNumber = 1000;

} // namespace

// ---------------------------------------------------------------------------------------------
// Context

bool RecorderScene::can(const menu::MenuContext& ctx) {
	// `process()`'s success arm dereferences `soundEditor.currentSound`, `currentSource` and
	// `currentMultiRange` with no null test at all, and `opened()` dereferences
	// `soundEditor.currentSound` as a `SoundDrum*` on a kit. Legacy gets away with it
	// because `checkPermissionToBeginSessionForRangeSpecificParam` refused first; this layer
	// resolves the context itself, so it asks here.
	if (!ctx.addresses.slot || ctx.range_index < 0) {
		return false;
	}
	// `AudioRecorder::opened`'s "If we're already recording (probably the output) then no!", which
	// is exactly `AudioEngine::foregroundRecordingSource() > AudioInputChannel::NONE`: the same
	// storage every recording publishes to.
	//
	// `AudioEngine::isAnyInternalRecordingHappening()` is NOT the same predicate: it misses an external
	// (mic/line) recording entirely. See the class docstring.
	return AudioEngine::foregroundRecordingSource() <= AudioInputChannel::NONE;
}

// ---------------------------------------------------------------------------------------------
// Lifecycle

void RecorderScene::open(const menu::MenuContext& ctx) {
	ctx_ = ctx;
}

bool RecorderScene::name_drum() {
	// `opened()`'s kit arm: the row is named "REC" and the first number from 1 no other row's
	// name has (`Kit::makeDrumNameUnique(&name, 1)`). The sink refuses a name another row holds.
	if (!ctx_.is_kit) {
		return true;
	}
	if (ctx_.sink == nullptr || !ctx_.addresses.lane) {
		// A kit context with no row to name -- affect-entire, which `can()` already refuses -- leaves the
		// drum unnamed, where legacy would have dereferenced a null `SoundDrum*`.
		return true;
	}
	const bind::Target name{
	    .kind = bind::Target::Kind::MODEL, .path = "name", .group = deluge::project::SettingGroupId::LANE};
	// The row's own name counts as taken, as `makeDrumNameUnique` counts it: a row already called REC1 becomes REC2.
	const std::optional<deluge::project::SettingAddress> at = bind::setting_address(name, ctx_.addresses);
	const std::optional<deluge::serialization::Value> own =
	    at && ctx_.model != nullptr ? deluge::project::read_setting(*ctx_.model, *at, "name") : std::nullopt;
	for (int32_t n = 1; n <= kMaxRecNumber; n++) {
		const std::string candidate = "REC" + std::to_string(n);
		if (own && own->str() == candidate) {
			continue;
		}
		if (ctx_.sink->write_text(name, ctx_.addresses, candidate)) {
			return true;
		}
	}
	// `display->displayError(error); return false;`.
	notify::notice_error_code(static_cast<int16_t>(Error::UNSPECIFIED));
	return false;
}

bool RecorderScene::setup_recording() {
	// `deluge::recording`'s `setupRecordingToFile`, for the one call `AudioRecorder::opened` makes
	// makes: stereo when something is plugged in, mono otherwise, into `RECORD`.
	//
	// Main's `AudioRecorder::setupRecordingToFile` (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`) freezes
	// under `ALPHA_OR_BETA_VERSION` when `recordingSource > NONE`; the
	// equivalent here is that `on_enter` never reaches this with `recorder_` already set.
	const bool in_stereo = AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
	const AudioInputChannel mode = in_stereo ? AudioInputChannel::STEREO : AudioInputChannel::LEFT;
	recorder_ = AudioEngine::getNewRecorder(in_stereo ? 2 : 1, AudioRecordingFolder::RECORD, mode, false, false,
	                                        kInternalButtonPressLatency, false, nullptr, {false});
	if (recorder_ == nullptr) {
		// `display->displayError(Error::INSUFFICIENT_RAM); return false;`.
		notify::notice_error_code(static_cast<int16_t>(Error::INSUFFICIENT_RAM));
		return false;
	}
	recorder_->allowFileAlterationAfter = true;
	// `shouldNormalize` defaults true and `opened()` does not pass it.
	recorder_->allowNormalization = true;
	// `recorder->monitorInputLevels = display->haveOLED() && isThresholdRecording()`: the render keeps measuring the
	// input for the meter after the threshold has triggered. Every panel this scene draws on is an OLED.
	recorder_->monitorInputLevels.store(threshold_take(), std::memory_order_relaxed);

	// `recordingSource = newMode` (main's `AudioRecorder::setupRecordingToFile`, "This sets recording to begin
	// happening even as the file is created"). **This is not bookkeeping.** Nine subsystems read the pair (`recorder`,
	// `recordingSource`) as "a foreground recording is live" -- most consequentially `AudioEngine::setMonitoringMode`
	// (`audio_engine.cpp`), which enables input monitoring ONLY for `STEREO`/`stereo`-or-`LEFT`, i.e. exactly the two
	// channels this line arms, and derives `monitoringAction` from the recorder pointer. A port that skipped it would
	// record correctly and leave the user hearing nothing while they did it.
	AudioEngine::setForegroundRecording(recorder_, mode);
	return true;
}

bool RecorderScene::on_enter(const SceneId& id) {
	(void)id;                   // No instance argument: there is exactly one recorder screen.
	last_enter_refused_ = true; // until the take has begun
	// `opened()`'s first refusal, ahead of the undo-log wipe so a refused take leaves the history intact: a load is
	// running that may swap out what the take would attach to.
	if (ctx_.facts != nullptr && ctx_.facts->output_may_be_replaced()) {
		notify::notice_text(kLoading);
		return false;
	}
	if (!can(ctx_)) {
		return false;
	}
	if (recorder_ != nullptr) {
		// The other half of legacy's "already recording" refusal -- this scene's own
		// recorder still being armed. A `Scene` is a singleton, so a second entry really can find
		// one.
		return false;
	}

	stopping_ = false;
	blink_anchor_ms_.reset();
	blink_fired_step_ = UINT32_MAX;

	// `opened()`'s first two statements. `updatedRecordingStatus` has no member here
	// (see the class docstring); the log wipe does, because the recording is about to make every
	// undo step meaningless.
	actionLogger.deleteAllLogs();

	if (!name_drum()) {
		return false;
	}
	// `AudioRecorder::opened`'s `PadLEDs::clearTickSquares(true)`.
	menu::host_clear_tick_squares();
	// `beginRecording`'s `blinkLed(BACK)` and `blinkLed(RECORD, 255, 1)` (`AudioRecorder::opened`) on success.
	blink_restart_ = setup_recording();
	if (blink_restart_) {
		target_ = take_target();
	}
	// `opened()`'s last statement, whether the take began or not.
	menu::host_end_audition(menu::AuditionEnd::ClipViewWhenAuditioning);
	last_enter_refused_ = !blink_restart_;
	return blink_restart_;
}

void RecorderScene::on_exit() {
	// The screen is removed from outside while its take runs. What the take was for may be going too, so the take ends
	// and its file stays on the card, attached to nothing; the foreground recording frees the recorder once it has
	// settled (`deluge::recording::slowRoutine`). The ordinary close never gets here with a recorder:
	// `after_render`'s completion arm has already finished it. Only the "Working" this scene showed is taken down.
	const bool showed_working = recorder_ != nullptr && stopping_;
	orphan_take();
	if (showed_working) {
		notify::notice_kind(notify::Notice::Kind::ClearWorking);
	}
}

void RecorderScene::taken_down() {
	// The whole stack is going (a song load or a clear), outside the guest's tick. A "Working" this scene showed is the
	// shell's, which the host disarms with the rest of the guest; a `ClearWorking` posted now would reach the legacy
	// display instead and take down the animation the taker-down put up ("Loading").
	orphan_take();
}

void RecorderScene::orphan_take() {
	if (recorder_ != nullptr) {
		deluge::recording::orphanInputTake();
		recorder_ = nullptr;
	}
	target_ = {};
	stopping_ = false;
}

// ---------------------------------------------------------------------------------------------
// The recording

bool RecorderScene::capturing() const {
	return recorder_ != nullptr && recorder_->numSamplesCaptured > recorder_->minThresholdMargin;
}

bool RecorderScene::threshold_take() const {
	return recorder_ != nullptr && recorder_->startValueThreshold > 0;
}

void RecorderScene::end_recording_soon() {
	// `deluge::recording::endRecordingSoon`. The status test is legacy's own guard against
	// being called twice, which it says "a few scenarios" can do.
	if (recorder_ == nullptr || recorder_->status != RecorderStatus::CAPTURING_DATA) {
		return;
	}
	notify::notice_text(kWorking, notify::Notice::Kind::Working);
	stopping_ = true;
	recorder_->endSyncedRecording(kInternalButtonPressLatency);
}

void RecorderScene::attach_sample() {
	// `process()`'s success arm, as one request: the app stops the sound's voices, makes the
	// source a sample source and puts the take, found in memory by its path, on the range.
	if (ctx_.sink == nullptr) {
		return;
	}
	// `RuntimeFacts::take_range`: the take lands only on the range it began on, wherever that range now sits, and only
	// while the same clip of the same song still plays its sound. Otherwise it stays on the card, attached to nothing,
	// with no popup.
	const std::optional<uint16_t> range = ctx_.facts != nullptr ? ctx_.facts->take_range(target_) : std::nullopt;
	if (!range) {
		return;
	}
	if (!ctx_.sink->attach_recording(target_.slot, target_.source, *range, recorder_->sample->filePath.c_str())) {
		// The take is on the card, but the sound did not get it.
		notify::notice_error_code(static_cast<int16_t>(Error::UNSPECIFIED));
	}
}

bind::TakeTarget RecorderScene::take_target() const {
	// The clip, the sound, its source and its range as the take begins, each by the identity that names that object
	// alone, so a sound or a range that later takes its place is not mistaken for it.
	const auto source = static_cast<uint8_t>(ctx_.source_index);
	return bind::TakeTarget{
	    .clip = ctx_.addresses.clip,
	    .slot = *ctx_.addresses.slot,
	    .source = source,
	    .range = ctx_.facts != nullptr
	                 ? ctx_.facts->range_identity(*ctx_.addresses.slot, source, static_cast<uint16_t>(ctx_.range_index))
	                 : std::nullopt,
	};
}

void RecorderScene::finish_recording() {
	// `deluge::recording`'s `finishRecording`. Unpublishing the channel is `recorder_ =
	// nullptr` here; `display->removeLoadingAnimation()` is the `ClearWorking` notice.
	//
	// **The caller must have checked `SampleRecorder::settled()` first** -- this frees, and
	// `after_render()` is the one call site. See the arm there for why.
	recorder_->pointerHeldElsewhere = false;
	// Cleared before `discardRecorder`, as `deluge::recording`'s `finishRecording` does (`main`'s
	// `AudioRecorder::finishRecording` clears after it, so for the length of that call the published
	// recorder names a freed object while the channel still says a recording is live -- and
	// `setMonitoringMode`, which dereferences that pointer, runs from the audio routine). Clearing
	// first closes the window and changes nothing else: no reader wants either value *during* the
	// discard.
	AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE);
	AudioEngine::discardRecorder(recorder_);
	recorder_ = nullptr;
	stopping_ = false;
	notify::notice_kind(notify::Notice::Kind::ClearWorking);
}

Result RecorderScene::after_render() {
	Result r;
	if (recorder_ == nullptr) {
		return r;
	}

	// `AudioRecorder::tick`'s completion arm.
	// A card error ends the take: stop capturing, then wait, as for any take, until the recorder has closed its file
	// and the driver holds nothing of it. Freeing it any earlier frees memory a write may still be reading.
	if (recorder_->hadCardError) {
		end_recording_soon();
	}
	// On a settled recorder: closed, with no cluster write still out. `finish_recording()` frees the recorder, and a
	// write still out borrows its buffer -- a take that ended on a card error can have one. Until then `next_timer()`
	// re-polls at `kRecorderPollMs`. The whole arm waits, not just the free, because `attach_sample()` publishes a
	// sample the recorder still owns -- legacy does both in one step and so does this.
	if (recorder_->settled()) {
		// The `if (status == ABORTED || hadCardError) {}` empty arm is legacy's way of
		// saying "attach nothing"; spelled as the condition on the attach instead.
		if (recorder_->status != RecorderStatus::ABORTED && !recorder_->hadCardError) {
			attach_sample();
		}
		finish_recording();
		// `close()`. `Remove` and not `Pop`: a `Working` or `Popup` overlay can legally be
		// on top of this scene by the time the router applies the action -- the `Working` this very
		// scene raised on the way to stopping, in fact -- and a blind `Pop` would take that instead.
		// See the note on `Action::Kind::Pop` in `shell/scene.h`.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kRecorderSceneKind});
		return r;
	}

	// `AudioRecorder::process`'s ongoing arm. `popup_if_free` is the `!display->hasPopup()` gate: the flag clears
	// whether or not the popup shows.
	if (recorder_->recordingClippedRecently) {
		recorder_->recordingClippedRecently = false;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Popup,
		                                 .surface = SurfaceRole::Main,
		                                 .popup_string = sid(String::STRING_FOR_CLIPPING_OCCURRED),
		                                 .popup_if_free = true});
	}
	// The `Waiting` -> `Recording` flip is legacy's third arm. Nothing to do here:
	// `render_oled` reads `capturing()` on every frame, and the repaint gate it arms is what makes
	// the next frame happen.
	return r;
}

// ---------------------------------------------------------------------------------------------
// Input

Result RecorderScene::handle(const UiEvent& event, const UiContext& ctx) {
	(void)ctx;
	Result r;

	// `AudioRecorder::buttonAction`: BACK, SELECT_ENC and RECORD all mean "stop", on
	// press only, and everything else falls through to whatever is beneath. Applied here, before the
	// tick's render, so the frame that follows already shows the recorder stopped.
	const bool is_button = event.kind == UiEvent::Kind::Button || event.kind == UiEvent::Kind::EncoderButton;
	if (!is_button || !event.on) {
		return Result::unhandled();
	}
	if (event.button != ButtonId::Back && event.button != ButtonId::SelectEnc && event.button != ButtonId::Record) {
		return Result::unhandled();
	}

	// Legacy answers `REMIND_ME_OUTSIDE_CARD_ROUTINE` while the card routine is running
	// so the press is retried rather than dropped. That deferral is `InputLowering::set_sd_busy`'s,
	// not a scene's, and emphatically not `Action::Kind::LockInput`'s -- see the class docstring.
	end_recording_soon();
	r.handled = true;
	return r;
}

std::optional<uint32_t> RecorderScene::next_timer() const {
	// The poll, and the reason this scene must be woken with no input at all -- see
	// `kRecorderPollMs`. Nothing else would render it, and `after_render()` is the only place the
	// finished recording can be noticed.
	if (recorder_ == nullptr) {
		return std::nullopt;
	}
	return kRecorderPollMs;
}

// ---------------------------------------------------------------------------------------------
// Rendering

void RecorderScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	(void)role;
	const uint32_t now = f.now_ms();

	if (threshold_take()) {
		deluge::ui::oled::threshold_meter(f, meter_state(f.bounds().w));
	}
	else {
		// `renderOLED`'s plain arm, through the toolkit rather than through `Canvas::drawStringCentred`
		// (`scenes-no-frame-primitives`). `MetricBold13` is the face `kTextBigSizeY` (13) selects; the centring is over
		// the frame's own width, which is legacy's `OLED_MAIN_WIDTH_PIXELS` on the shipped panel.
		deluge::ui::oled::text(
		    f, deluge::gfx::oled::Rect{0, kStatusY, f.bounds().w, deluge::gfx::oled::kTextBigSizeY},
		    capturing() ? kRecording : kWaiting,
		    {.font = deluge::gfx::oled::FontId::MetricBold13, .align = deluge::ui::oled::Align::Centre});
	}

	// This arm is not only about the `Waiting` -> `Recording` flip: `after_render()` runs only for a
	// scene that drew this tick, so without a repaint the completion poll stops the moment the last
	// input arrives, and the scene sits on "Waiting" over a recorder that finished minutes ago.
	// `next_timer()` alone wakes the shell without marking the panel dirty
	// (`RepaintGate::needs_frame`), so this line is the tighter half of a pair with
	// `Shell::tick`'s step 8b, which arms from `next_timer()` itself every
	// surface this scene claims and is not covered on. Legacy has no equivalent either way, because `process()` is a
	// busy loop with `OLED::sendMainImage()` inside it.
	if (recorder_ != nullptr) {
		f.gate().request_repaint_after(kRecorderPollMs, now);
	}
}

deluge::ui::oled::ThresholdMeter RecorderScene::meter_state(int32_t width) const {
	// The meter's ends from the input's levels (a mono take shows its one channel on both), the threshold's x, and the
	// blink, on the audio clock legacy blinks by.
	const auto animation_ms =
	    static_cast<uint32_t>(static_cast<uint64_t>(AudioEngine::audioSampleTimer) * 1000 / kSampleRate);
	const auto level = recorder_->getInputLevels();
	const float right = recorder_->recordingNumChannels == 2 ? level.r : level.l;
	deluge::ui::oled::ThresholdMeter m{
	    .level_end_x = {deluge::ui::oled::threshold_meter_x(level.l, width),
	                    deluge::ui::oled::threshold_meter_x(right, width)},
	    .threshold_x = deluge::ui::oled::threshold_meter_x(recorder_->startValueThreshold, width),
	    .recording = capturing(),
	};
	if (m.recording) {
		m.blink_on = (animation_ms / kRecordDotHalfPeriodMs) % 2 == 0;
	}
	else {
		const uint32_t phase = animation_ms % kThresholdBlinkCycleMs;
		m.blink_on = phase < kThresholdLitUntilMs || phase >= kThresholdLitFromMs;
	}
	return m;
}

void RecorderScene::render_grid(deluge::ui::grid::GridFrame& f) {
	// The pads are the view's beneath. `opened()` arms the blink only on success, and leaves the source blinks as the
	// sound editor had them; the items the recorder opens from blink no patching source, so the plan is the one pad.
	if (recorder_ == nullptr) {
		return;
	}
	const uint32_t now = f.now_ms();
	if (!blink_anchor_ms_.has_value()) {
		blink_anchor_ms_ = now;
	}
	const menu::BlinkPlan plan =
	    menu::exclusive_blink(static_cast<uint8_t>(std::max<int32_t>(ctx_.source_index, 0)), kRecordShortcutY, 0);
	const uint32_t step = menu::blink_step(*blink_anchor_ms_, now);
	if (step != blink_fired_step_) {
		menu::fire_blink_step(f, plan, step);
		blink_fired_step_ = step;
	}
	f.grid_gate().request_repaint_after(menu::ms_to_next_step(*blink_anchor_ms_, now), now);
}

void RecorderScene::leds(deluge::ui::LedFrame& f) {
	// `opened()`'s LED tail, all seven of it. `LedId` values are `ButtonId` values by
	// the contract in `shell/profiles/deluge.h`, and legacy's own `IndicatorLED` values are the
	// same button matrix coordinates (`hid/led/indicator_leds.h`), so each of these is the
	// same lamp.
	//
	// SYNTH and KIT are declared here and NOT left to `MenuScene` beneath, even though that scene also names them:
	// `SoundEditor::setLedStates` turns **both** off (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) and this UI
	// then lights exactly one of them, by `soundEditor.editingKit()` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`,
	// which is `getCurrentOutputType() == OutputType::KIT` -- the context's own `is_kit`). `LedCompose::compose` runs
	// the stack bottom to top and the last writer wins (`shell/leds.h`), so declaring them here is what makes the
	// recorder's answer the one that shows.
	constexpr deluge::ui::LedState kOff{deluge::ui::LedState::Kind::Off, 0, 0};
	constexpr deluge::ui::LedState kOn{deluge::ui::LedState::Kind::On, 0, 0};
	if (blink_restart_) {
		blink_restart_ = false;
		f.restart(deluge::ui::BlinkRate::Normal);
		f.restart(deluge::ui::BlinkRate::Fast);
	}
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Synth), ctx_.is_kit ? kOff : kOn);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Kit), ctx_.is_kit ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::CrossScreen), kOff);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Session), kOff);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Scale), kOff);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Back),
	            deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal));
	// `blinkLed(RECORD, 255, 1)` -- 255 blinks means "keep going", at blink-type 1's fast rate, which is "blink until
	// this scene stops declaring it".
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Record),
	            deluge::ui::LedState::shared(deluge::ui::BlinkRate::Fast));
}

RecorderScene& recorder_scene() {
	static RecorderScene scene;
	return scene;
}

} // namespace deluge::scenes::recorder
