/// @file
/// @brief `RecorderScene`. See `recorder_scene.h`.

#include "scenes/recorder/recorder_scene.h"

#include "ui/oled/frame.h"
#include "ui/oled/text.h"

#include "notify/notice_queue.h"

#include "gui/l10n/strings.h"
#include "model/action/action_logger.h"
#include "model/instrument/kit.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"

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

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

/// The two words `renderOLED` draws (`audio_recorder.cpp:117`). Literals, not l10n ids, because
/// neither is in `gui/l10n/strings.h` -- exactly `BrowserScene::Descriptor::title`'s and
/// `MarkerScene`'s situation and answer.
/// @{
constexpr std::string_view kWaiting = "Waiting";
constexpr std::string_view kRecording = "Recording";
/// @}

/// `display->displayLoadingAnimationText("Working")` (`:163`), raised the way `browser::Listing`
/// raises the same thing: `Notices::drain` turns a `Working` notice into `SceneKind::Working` over
/// this scene (`shell/notices.cpp`), and `ClearWorking` takes it away again.
constexpr std::string_view kWorking = "Working";

} // namespace

// ---------------------------------------------------------------------------------------------
// Context

bool RecorderScene::can(const menu::MenuContext& ctx) {
	// `process()`'s success arm dereferences `soundEditor.currentSound`, `currentSource` and
	// `currentMultiRange` with no null test at all (`:213-217`), and `opened()` dereferences
	// `soundEditor.currentSound` as a `SoundDrum*` on a kit (`:76`). Legacy gets away with it
	// because `checkPermissionToBeginSessionForRangeSpecificParam` refused first; this layer
	// resolves the context itself, so it asks here.
	if (ctx.sound == nullptr || ctx.source == nullptr || ctx.range == nullptr) {
		return false;
	}
	// `opened()`'s "If we're already recording (probably the output) then no!" (`:69-71`). See the
	// class docstring: `recordingSource` is a member of the legacy object this layer may not name,
	// and every external writer of it goes through `beginOutputRecording`, i.e. an internal channel.
	return !AudioEngine::isAnyInternalRecordingHappening();
}

// ---------------------------------------------------------------------------------------------
// Lifecycle

void RecorderScene::open(const menu::MenuContext& ctx) {
	ctx_ = ctx;
}

bool RecorderScene::name_drum() {
	// `opened()`'s kit arm (`:74-88`).
	if (!ctx_.is_kit) {
		return true;
	}
	auto* kit = static_cast<Kit*>(ctx_.output);
	auto* drum = static_cast<SoundDrum*>(ctx_.sound);
	if (kit == nullptr || drum == nullptr) {
		// DIVERGENCE, labelled rather than inherited: legacy casts `soundEditor.currentSound`
		// straight to `SoundDrum*` and dereferences it. A kit context with affect-entire on has a
		// null `sound`, which `can()` already refuses -- so this is unreachable today and reads as
		// "leave the drum unnamed" rather than crashing if that stops being true.
		return true;
	}
	std::string new_name = "REC";
	const Error error = kit->makeDrumNameUnique(&new_name, 1);
	if (error != Error::NONE) {
		// `display->displayError(error); return false;` (`:82-84`).
		notify::notice_error_code(static_cast<int16_t>(error));
		return false;
	}
	drum->drumName = new_name.c_str();
	return true;
}

bool RecorderScene::setup_recording() {
	// `AudioRecorder::setupRecordingToFile` (`:121-141`), for the one call `opened()` makes
	// (`:94-95`): stereo when something is plugged in, mono otherwise, into `RECORD`.
	//
	// Legacy's `ALPHA_OR_BETA_VERSION` freeze at `:124` guards `recordingSource > NONE`; the
	// equivalent here is that `on_enter` never reaches this with `recorder_` already set.
	const bool in_stereo = AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
	const AudioInputChannel mode = in_stereo ? AudioInputChannel::STEREO : AudioInputChannel::LEFT;
	recorder_ = AudioEngine::getNewRecorder(in_stereo ? 2 : 1, AudioRecordingFolder::RECORD, mode, false, false,
	                                        kInternalButtonPressLatency, false, nullptr, {false});
	if (recorder_ == nullptr) {
		// `display->displayError(Error::INSUFFICIENT_RAM); return false;` (`:130-133`).
		notify::notice_error_code(static_cast<int16_t>(Error::INSUFFICIENT_RAM));
		return false;
	}
	recorder_->allowFileAlterationAfter = true;
	// `shouldNormalize` defaults true and `opened()` does not pass it (`:94`).
	recorder_->allowNormalization = true;
	return true;
}

bool RecorderScene::on_enter(const SceneId& id) {
	(void)id; // No instance argument: there is exactly one recorder screen.
	if (!can(ctx_)) {
		return false;
	}
	if (recorder_ != nullptr) {
		// The other half of legacy's "already recording" refusal (`:69-71`) -- this scene's own
		// recorder still being armed. A `Scene` is a singleton, so a second entry really can find
		// one.
		return false;
	}

	stopping_ = false;

	// `opened()`'s first two statements (`:64-66`). `updatedRecordingStatus` has no member here
	// (see the class docstring); the log wipe does, because the recording is about to make every
	// undo step meaningless.
	actionLogger.deleteAllLogs();

	if (!name_drum()) {
		return false;
	}
	return setup_recording();
}

void RecorderScene::on_exit() {
	// **A divergence with no legacy counterpart, because legacy cannot reach it.** `process()`
	// blocks inside the menu item until it has called `close()` itself, so the recorder UI is never
	// torn down from outside. A scene can be: an external `Pop`, a `replace_root`, a song load. A
	// recorder left armed there would keep capturing into a file nothing owns, and
	// `finishRecording`'s `discardRecorder` would never run.
	//
	// So an armed recorder is thrown away exactly as `AudioClip::abortRecording` throws one away
	// (`model/clip/audio_clip.cpp:120-132`): clear `pointerHeldElsewhere` *before* `abort()`, which
	// is that function's own load-bearing comment, and drop the pointer. `abort()` deletes the
	// part-written file; `doRecorderCardRoutines` reaps the object.
	//
	// The ordinary close path never gets here with a recorder: `after_render`'s completion arm has
	// already run `finish_recording()`.
	if (recorder_ != nullptr) {
		recorder_->pointerHeldElsewhere = false;
		recorder_->abort();
		recorder_ = nullptr;
		notify::notice_kind(notify::Notice::Kind::ClearWorking);
	}
	stopping_ = false;
}

// ---------------------------------------------------------------------------------------------
// The recording

bool RecorderScene::capturing() const {
	return recorder_ != nullptr && recorder_->numSamplesCaptured != 0;
}

void RecorderScene::end_recording_soon() {
	// `AudioRecorder::endRecordingSoon` (`:159-166`). The status test is legacy's own guard against
	// being called twice, which it says "a few scenarios" can do.
	if (recorder_ == nullptr || recorder_->status != RecorderStatus::CAPTURING_DATA) {
		return;
	}
	notify::notice_text(kWorking, notify::Notice::Kind::Working);
	stopping_ = true;
	recorder_->endSyncedRecording(kInternalButtonPressLatency);
}

void RecorderScene::attach_sample() {
	// `process()`'s success arm (`:212-218`), through the context rather than through
	// `soundEditor.current*`.
	ctx_.sound->killAllVoices();
	ctx_.source->setOscType(OscType::SAMPLE);
	AudioFileHolder* holder = ctx_.range->getAudioFileHolder();
	if (holder == nullptr) {
		return;
	}
	holder->filePath = recorder_->sample->filePath;
	holder->setAudioFile(recorder_->sample, ctx_.source->sampleControls.isCurrentlyReversed(), true);
}

void RecorderScene::finish_recording() {
	// `AudioRecorder::finishRecording` (`:245-253`). `recordingSource = NONE` is `recorder_ =
	// nullptr` here; `display->removeLoadingAnimation()` is the `ClearWorking` notice.
	//
	// Legacy's own note on `slowRoutine` (`:168-175`) says `discardRecorder` must not run from
	// inside the SD routine -- and `process()`, which is the path this arm ports, calls it
	// unguarded from the main loop for exactly that reason. `after_render()` is the same place in
	// the same sense: the shell's tick, not a card routine. The guard belongs to the
	// output-recording reaper, which is not this scene's (see the class docstring).
	recorder_->pointerHeldElsewhere = false;
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

	// `process()`'s completion arm (`:207-223`).
	if (recorder_->status >= RecorderStatus::COMPLETE || recorder_->hadCardError) {
		// The `if (status == ABORTED || hadCardError) {}` empty arm at `:209` is legacy's way of
		// saying "attach nothing"; spelled as the condition on the attach instead.
		if (recorder_->status != RecorderStatus::ABORTED && !recorder_->hadCardError) {
			attach_sample();
		}
		finish_recording();
		// `close()` (`:221`). `Remove` and not `Pop`: a `Working` or `Popup` overlay can legally be
		// on top of this scene by the time the router applies the action -- the `Working` this very
		// scene raised on the way to stopping, in fact -- and a blind `Pop` would take that instead.
		// See the note on `Action::Kind::Pop` in `shell/scene.h`.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kRecorderSceneKind});
		return r;
	}

	// `process()`'s ongoing arm (`:226-240`). The `!display->hasPopup()` gate at `:230` is the
	// popup service's own business here: `Notices` decides whether a popup is already up, which is
	// the same question asked one layer further out.
	if (recorder_->recordingClippedRecently) {
		recorder_->recordingClippedRecently = false;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Popup,
		                                 .surface = SurfaceRole::Main,
		                                 .popup_string = sid(String::STRING_FOR_CLIPPING_OCCURRED)});
	}
	// The `Waiting` -> `Recording` flip is legacy's third arm (`:234-239`). Nothing to do here:
	// `render_oled` reads `capturing()` on every frame, and the repaint gate it arms is what makes
	// the next frame happen.
	return r;
}

// ---------------------------------------------------------------------------------------------
// Input

Result RecorderScene::handle(const UiEvent& event, const UiContext& ctx) {
	(void)ctx;
	Result r;

	// `AudioRecorder::buttonAction` (`:255-280`): BACK, SELECT_ENC and RECORD all mean "stop", on
	// press only, and everything else falls through to whatever is beneath.
	//
	// Applied HERE and not parked for `after_render`: `Back` and `SelectEnc` are two of the six
	// shapes the shell delivers *after* every `after_render()` call (`Shell::tick` step 8), so
	// parking one would cost a whole tick -- E3 task 3's finding, and task 4 repeats it.
	const bool is_button = event.kind == UiEvent::Kind::Button || event.kind == UiEvent::Kind::EncoderButton;
	if (!is_button || !event.on) {
		return Result::unhandled();
	}
	if (event.button != ButtonId::Back && event.button != ButtonId::SelectEnc && event.button != ButtonId::Record) {
		return Result::unhandled();
	}

	// Legacy answers `REMIND_ME_OUTSIDE_CARD_ROUTINE` while the card routine is running (`:270-272`)
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

	// `renderOLED` (`:116-119`), through the toolkit rather than through `Canvas::drawStringCentred`
	// (`scenes-no-frame-primitives`). `MetricBold13` is the face `kTextBigSizeY` (13) selects
	// (`gfx/oled/font.h:14`); the centring is over the frame's own width, which is legacy's
	// `OLED_MAIN_WIDTH_PIXELS` on the shipped panel.
	deluge::ui::oled::text(f, deluge::gfx::oled::Rect{0, kStatusY, f.bounds().w, deluge::gfx::oled::kTextBigSizeY},
	                       capturing() ? kRecording : kWaiting,
	                       {.font = deluge::gfx::oled::FontId::MetricBold13, .align = deluge::ui::oled::Align::Centre});

	// **And this arm is what makes the recording finish at all.** It is not only about the
	// `Waiting` -> `Recording` flip: `after_render()` runs only for a scene that drew this tick, so
	// without it the completion poll stops the moment the last input arrives, and the scene sits on
	// "Waiting" over a recorder that finished minutes ago. `next_timer()` alone wakes the shell; it
	// does not mark the panel dirty (`RepaintGate::needs_frame`). Legacy has no equivalent because
	// `process()` is a busy loop with `OLED::sendMainImage()` inside it.
	if (recorder_ != nullptr) {
		f.gate().request_repaint_after(kRecorderPollMs, now);
	}
}

void RecorderScene::leds(deluge::ui::LedFrame& f) {
	// `opened()`'s LED tail (`:100-106`), all seven of it. `LedId` values are `ButtonId` values by
	// the contract in `shell/profiles/deluge.h:19`, and legacy's own `IndicatorLED` values are the
	// same button matrix coordinates (`hid/led/indicator_leds.h:36-65`), so each of these is the
	// same lamp.
	//
	// SYNTH and KIT are declared here and NOT left to `MenuScene` beneath, even though that scene
	// also names them: `SoundEditor::setLedStates` turns **both** off (`sound_editor.cpp:306-307`)
	// and this UI then lights exactly one of them, by `soundEditor.editingKit()`
	// (`sound_editor.cpp:161-163`, which is `getCurrentOutputType() == OutputType::KIT` -- the
	// context's own `is_kit`). `LedCompose::compose` runs the stack bottom to top and the last
	// writer wins (`shell/leds.h`), so declaring them here is what makes the recorder's answer the
	// one that shows.
	constexpr deluge::ui::LedState kOff{deluge::ui::LedState::Kind::Off, 0, 0};
	constexpr deluge::ui::LedState kOn{deluge::ui::LedState::Kind::On, 0, 0};
	/// `blinkLed`'s two rates: type 0 is `kFlashTime` (110 ms) and type 1 `kFastFlashTime` (60 ms)
	/// (`hid/led/indicator_leds.cpp:82-90`, `definitions_cxx.hpp:159-160`).
	constexpr uint16_t kFlashMs = 110;
	constexpr uint16_t kFastFlashMs = 60;
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Synth), ctx_.is_kit ? kOff : kOn);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Kit), ctx_.is_kit ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::CrossScreen), kOff);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Session), kOff);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Scale), kOff);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Back),
	            deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, kFlashMs, 0});
	// `blinkLed(RECORD, 255, 1)` (`:106`) -- 255 blinks means "keep going", at blink-type 1's fast
	// rate. `LedState::Blink` has a period and a deadline but no count, so the 255 has nothing to
	// map to and is simply "blink until this scene stops declaring it", which is what 255 meant.
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Record),
	            deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, kFastFlashMs, 0});
}

RecorderScene& recorder_scene() {
	static RecorderScene scene;
	return scene;
}

} // namespace deluge::scenes::recorder
