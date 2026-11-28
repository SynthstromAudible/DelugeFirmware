/// @file
/// @brief `RecorderScene`. See `recorder_scene.h`.

#include "ui/scenes/recorder/recorder_scene.h"

#include "base/error.h"
#include "ui/views/grid/blink.h"
#include "ui/views/oled/recorder.h"
#include "ui/widgets/oled/frame.h"

#include "base/notify/notice_queue.h"

#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/bind/target.h"
#include "ui/widgets/grid/frame.h"

#include "gui/l10n/strings.h"
#include "project/setting_group.h"
#include "project/song.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace deluge::ui::scenes::recorder {

using deluge::l10n::String;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

/// `display->displayLoadingAnimationText("Working")`, raised the way `browser::Listing`
/// raises the same thing: `Notices::drain` turns a `Working` notice into `SceneKind::Working` over
/// this scene (`ui/shell/notices.cpp`), and `ClearWorking` takes it away again.
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
	// `AudioRecorder::opened`'s "If we're already recording (probably the output) then no!": any source the foreground
	// recording names, input or internal, refuses. A host with no audio port reports nothing recording. See the class
	// docstring.
	const bind::AudioFacts* a = ctx.ports.audio;
	const bind::ForegroundRecording f = a != nullptr ? a->foreground_recording() : bind::ForegroundRecording::NONE;
	return f == bind::ForegroundRecording::UNSET || f == bind::ForegroundRecording::NONE;
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
	if (ctx_.ports.writes == nullptr || !ctx_.addresses.lane) {
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
		if (ctx_.ports.writes->write_text(name, ctx_.addresses, candidate)) {
			return true;
		}
	}
	// `display->displayError(error); return false;`.
	base::notify::notice_error_code(static_cast<int16_t>(Error::UNSPECIFIED));
	return false;
}

bool RecorderScene::setup_recording() {
	// `AudioRecorder::setupRecordingToFile`, for the one call `AudioRecorder::opened` makes: stereo when something is
	// plugged in, mono otherwise. The take port begins it and publishes it as the foreground recording in one step.
	const bool stereo = ctx_.ports.audio != nullptr && ctx_.ports.audio->audio_inputs_present();
	bind::TakeActions* const t = takes();
	// A take is begun only where the scene can read it back, or nothing would ever finish it; without either port
	// the entry fails silently.
	const bool can_take = t != nullptr && ctx_.ports.audio != nullptr;
	armed_ = can_take && t->begin(stereo ? bind::TakeChannels::STEREO : bind::TakeChannels::MONO);
	if (can_take && !armed_) {
		// `display->displayError(Error::INSUFFICIENT_RAM); return false;`.
		base::notify::notice_error_code(static_cast<int16_t>(Error::INSUFFICIENT_RAM));
	}
	return armed_;
}

std::optional<bind::InputTake> RecorderScene::take() const {
	if (!armed_ || ctx_.ports.audio == nullptr) {
		return std::nullopt;
	}
	return ctx_.ports.audio->input_take();
}

bind::TakeActions* RecorderScene::takes() const {
	return ctx_.ports.take_actions;
}

bool RecorderScene::on_enter(const SceneId& id) {
	(void)id;                   // No instance argument: there is exactly one recorder screen.
	last_enter_refused_ = true; // until the take has begun
	// `opened()`'s first refusal, ahead of the undo-log wipe so a refused take leaves the history intact: a load is
	// running that may swap out what the take would attach to.
	if (ctx_.ports.card != nullptr && ctx_.ports.card->output_may_be_replaced()) {
		base::notify::notice_text(kLoading);
		return false;
	}
	if (!can(ctx_)) {
		return false;
	}
	if (armed_) {
		// The other half of legacy's "already recording" refusal -- this scene's own take still
		// being held. A `Scene` is a singleton, so a second entry really can find one.
		return false;
	}

	stopping_ = false;
	blink_anchor_ms_.reset();
	blink_fired_step_ = UINT32_MAX;

	// `opened()`'s first two statements. `updatedRecordingStatus` has no member here
	// (see the class docstring); the log wipe does, because the recording is about to make every
	// undo step meaningless.
	if (auto* d = ctx_.ports.device_actions) {
		d->delete_all_logs();
	}

	if (!name_drum()) {
		return false;
	}
	// `AudioRecorder::opened`'s `PadLEDs::clearTickSquares(true)`.
	if (auto* i = ctx_.ports.indicators) {
		i->clear_tick_squares();
	}
	// `beginRecording`'s `blinkLed(BACK)` and `blinkLed(RECORD, 255, 1)` (`AudioRecorder::opened`) on success.
	blink_restart_ = setup_recording();
	if (blink_restart_) {
		target_ = take_target();
	}
	// `opened()`'s last statement, whether the take began or not.
	if (auto* v = ctx_.ports.view_actions) {
		v->end_audition(bind::AuditionEnd::ClipViewWhenAuditioning);
	}
	last_enter_refused_ = !blink_restart_;
	return blink_restart_;
}

void RecorderScene::on_exit() {
	// The screen is removed from outside while its take runs. What the take was for may be going too, so the take ends
	// and its file stays on the card, attached to nothing; the foreground recording frees the recorder once it has
	// settled (`TakeActions::orphan`). The ordinary close never gets here with a take held: `after_render`'s
	// completion arm has already finished it. Only the "Working" this scene showed is taken down.
	const bool showed_working = armed_ && stopping_;
	orphan_take();
	if (showed_working) {
		base::notify::notice_kind(base::notify::Notice::Kind::ClearWorking);
	}
}

void RecorderScene::taken_down() {
	// The whole stack is going (a song load or a clear), outside the guest's tick. A "Working" this scene showed is the
	// shell's, which the host disarms with the rest of the guest; a `ClearWorking` posted now would reach the legacy
	// display instead and take down the animation the taker-down put up ("Loading").
	orphan_take();
}

void RecorderScene::orphan_take() {
	if (armed_ && takes() != nullptr) {
		takes()->orphan();
	}
	armed_ = false;
	target_ = {};
	stopping_ = false;
}

// ---------------------------------------------------------------------------------------------
// The recording

bool RecorderScene::capturing() const {
	const std::optional<bind::InputTake> t = take();
	return t && t->capturing;
}

bool RecorderScene::threshold_take() const {
	const std::optional<bind::InputTake> t = take();
	return t && t->threshold;
}

void RecorderScene::end_recording_soon() {
	// `AudioRecorder::endRecordingSoon`. The still-writing test is legacy's own guard against
	// being called twice, which it says "a few scenarios" can do.
	const std::optional<bind::InputTake> t = take();
	if (!armed_ || !t || !t->capturing_data || takes() == nullptr) {
		return;
	}
	base::notify::notice_text(kWorking, base::notify::Notice::Kind::Working);
	stopping_ = true;
	takes()->end_soon();
}

void RecorderScene::attach_sample() {
	// `process()`'s success arm, as one request: the app stops the sound's voices, makes the
	// source a sample source and puts the take, found in memory by its path, on the range.
	if (ctx_.ports.sound_writes == nullptr) {
		return;
	}
	// `SoundFacts::take_range`: the take lands only on the range it began on, wherever that range now sits, and only
	// while the same clip of the same song still plays its sound. Otherwise it stays on the card, attached to nothing,
	// with no popup.
	const std::optional<uint16_t> range =
	    ctx_.ports.sound != nullptr ? ctx_.ports.sound->take_range(target_) : std::nullopt;
	if (!range) {
		return;
	}
	const std::optional<std::string> path =
	    ctx_.ports.audio != nullptr ? ctx_.ports.audio->input_take_path() : std::nullopt;
	if (!path) {
		return;
	}
	if (!ctx_.ports.sound_writes->attach_recording(target_.slot, target_.source, *range, path->c_str())) {
		// The take is on the card, but the sound did not get it.
		base::notify::notice_error_code(static_cast<int16_t>(Error::UNSPECIFIED));
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
	    .range = ctx_.ports.sound != nullptr ? ctx_.ports.sound->range_identity(*ctx_.addresses.slot, source,
	                                                                            static_cast<uint16_t>(ctx_.range_index))
	                                         : std::nullopt,
	};
}

void RecorderScene::finish_recording() {
	// `AudioRecorder::finishRecording`: the take port unpublishes and frees the take;
	// `display->removeLoadingAnimation()` is the `ClearWorking` notice.
	//
	// **The caller must have read the take settled first** -- this frees, and `after_render()` is the one call site.
	// See the arm there for why.
	if (auto* t = takes()) {
		t->finish();
	}
	armed_ = false;
	stopping_ = false;
	base::notify::notice_kind(base::notify::Notice::Kind::ClearWorking);
}

Result RecorderScene::after_render() {
	Result r;
	if (!take()) {
		return r;
	}

	// `AudioRecorder::tick`'s completion arm.
	// A card error ends the take: stop capturing, then wait, as for any take, until the recorder has closed its file
	// and the driver holds nothing of it. Freeing it any earlier frees memory a write may still be reading.
	if (take()->card_error) {
		end_recording_soon();
	}
	// On a settled take: closed, with no cluster write still out. `finish_recording()` frees the take, and a
	// write still out borrows its buffer -- a take that ended on a card error can have one. Until then `next_timer()`
	// re-polls at `kRecorderPollMs`. The whole arm waits, not just the free, because `attach_sample()` publishes a
	// sample the take still owns -- legacy does both in one step and so does this.
	const std::optional<bind::InputTake> now = take();
	if (now && now->settled) {
		// The `if (status == ABORTED || hadCardError) {}` empty arm is legacy's way of
		// saying "attach nothing"; spelled as the condition on the attach instead.
		if (!now->aborted && !now->card_error) {
			attach_sample();
		}
		finish_recording();
		// `close()`. `Remove` and not `Pop`: a `Working` or `Popup` overlay can legally be
		// on top of this scene by the time the router applies the action -- the `Working` this very
		// scene raised on the way to stopping, in fact -- and a blind `Pop` would take that instead.
		// See the note on `Action::Kind::Pop` in `ui/shell/scene.h`.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kRecorderSceneKind});
		return r;
	}

	// `AudioRecorder::process`'s ongoing arm. `popup_if_free` is the `!display->hasPopup()` gate: the flag clears
	// whether or not the popup shows.
	if (takes() != nullptr && takes()->take_clipped()) {
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
	if (!armed_) {
		return std::nullopt;
	}
	return kRecorderPollMs;
}

// ---------------------------------------------------------------------------------------------
// Rendering

void RecorderScene::render_oled(SurfaceRole role, deluge::ui::widgets::oled::OledFrame& f) {
	(void)role;
	const uint32_t now = f.now_ms();
	deluge::ui::views::oled::recorder_status(f, present_status(f.bounds().w));

	// This arm is not only about the `Waiting` -> `Recording` flip: `after_render()` runs only for a
	// scene that drew this tick, so without a repaint the completion poll stops the moment the last
	// input arrives, and the scene sits on "Waiting" over a recorder that finished minutes ago.
	// `next_timer()` alone wakes the shell without marking the panel dirty
	// (`RepaintGate::needs_frame`), so this line is the tighter half of a pair with
	// `Shell::tick`'s step 8b, which arms from `next_timer()` itself every
	// surface this scene claims and is not covered on. Legacy has no equivalent either way, because `process()` is a
	// busy loop with `OLED::sendMainImage()` inside it.
	if (armed_) {
		f.gate().request_repaint_after(kRecorderPollMs, now);
	}
}

void RecorderScene::render_grid(deluge::ui::widgets::grid::GridFrame& f) {
	// The pads are the view's beneath. `opened()` arms the blink only on success, and leaves the source blinks as the
	// sound editor had them; the items the recorder opens from blink no patching source, so the plan is the one pad.
	if (!armed_) {
		return;
	}
	const uint32_t now = f.now_ms();
	if (!blink_anchor_ms_.has_value()) {
		blink_anchor_ms_ = now;
	}
	const deluge::ui::views::grid::BlinkPads pads = present_blink(now);
	deluge::ui::views::grid::blink(f, pads);
	blink_fired_step_ = pads.step;
	f.grid_gate().request_repaint_after(deluge::ui::widgets::grid::ms_to_next_step(*blink_anchor_ms_, now), now);
}

void RecorderScene::leds(deluge::ui::widgets::core::LedFrame& f) {
	// `opened()`'s LED tail, all seven of it. `LedId` values are `ButtonId` values by
	// the contract in `ui/shell/profiles/deluge.h`, and legacy's own `IndicatorLED` values are the
	// same button matrix coordinates (`hid/led/indicator_leds.h`), so each of these is the
	// same lamp.
	//
	// SYNTH and KIT are declared here and NOT left to `MenuScene` beneath, even though that scene also names them:
	// `SoundEditor::setLedStates` turns **both** off (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) and this UI
	// then lights exactly one of them, by `soundEditor.editingKit()` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`,
	// which is `getCurrentOutputType() == OutputType::KIT` -- the context's own `is_kit`). `LedCompose::compose` runs
	// the stack bottom to top and the last writer wins (`ui/shell/leds.h`), so declaring them here is what makes the
	// recorder's answer the one that shows.
	constexpr deluge::ui::widgets::core::LedState kOff{deluge::ui::widgets::core::LedState::Kind::Off, 0, 0};
	constexpr deluge::ui::widgets::core::LedState kOn{deluge::ui::widgets::core::LedState::Kind::On, 0, 0};
	if (blink_restart_) {
		blink_restart_ = false;
		f.restart(deluge::ui::widgets::core::BlinkRate::Normal);
		f.restart(deluge::ui::widgets::core::BlinkRate::Fast);
	}
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Synth), ctx_.is_kit ? kOff : kOn);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Kit), ctx_.is_kit ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::CrossScreen), kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Session), kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Scale), kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Back),
	            deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Normal));
	// `blinkLed(RECORD, 255, 1)` -- 255 blinks means "keep going", at blink-type 1's fast rate, which is "blink until
	// this scene stops declaring it".
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Record),
	            deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Fast));
}

RecorderScene& recorder_scene() {
	static RecorderScene scene;
	return scene;
}

} // namespace deluge::ui::scenes::recorder
