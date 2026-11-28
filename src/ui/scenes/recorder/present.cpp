/// @file
/// @brief What the recorder scene's two surfaces show, as the plain structs the views draw. Const members: nothing here
///        writes the scene or reads a frame.

#include "base/sample_rate.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/recorder/recorder_scene.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace deluge::ui::scenes::recorder {

namespace {

/// The record dot's half-period while recording, and the threshold line's cycle while waiting, lit for its first 290 ms
/// and from 570 ms on (`AudioRecorder::getThresholdMeterState` (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`)).
/// @{
constexpr uint32_t kRecordDotHalfPeriodMs = 480;
constexpr uint32_t kThresholdBlinkCycleMs = 750;
constexpr uint32_t kThresholdLitUntilMs = 290;
constexpr uint32_t kThresholdLitFromMs = 570;
/// @}

/// The two words `AudioRecorder::renderOLED` draws. Literals, not l10n ids, because
/// neither is in `gui/l10n/strings.h` -- exactly `BrowserScene::Descriptor::title`'s and
/// `MarkerScene`'s situation and answer.
/// @{
constexpr std::string_view kWaiting = "Waiting";
constexpr std::string_view kRecording = "Recording";
/// @}

} // namespace

views::oled::RecorderStatus RecorderScene::present_status(int32_t width) const {
	if (!threshold_take()) {
		return {
		    .mode = views::oled::RecorderStatus::Mode::Word, .word = capturing() ? kRecording : kWaiting, .meter = {}};
	}
	// The meter's ends from the input's levels (a mono take shows its one channel on both), the threshold's x, and the
	// blink, on the audio clock legacy blinks by.
	const uint32_t frames = ctx_.ports.audio != nullptr ? ctx_.ports.audio->audio_sample_timer() : 0;
	const auto animation_ms = static_cast<uint32_t>(static_cast<uint64_t>(frames) * 1000 / base::kSampleRate);
	const bind::InputLevels level = takes() != nullptr ? takes()->take_levels() : bind::InputLevels{};
	const bind::InputTake t = take().value_or(bind::InputTake{});
	const float right = t.channels == 2 ? level.right : level.left;
	widgets::oled::ThresholdMeter m{
	    .level_end_x = {widgets::oled::threshold_meter_x(level.left, width),
	                    widgets::oled::threshold_meter_x(right, width)},
	    .threshold_x = widgets::oled::threshold_meter_x(t.threshold_level, width),
	    .recording = t.capturing,
	};
	if (m.recording) {
		m.blink_on = (animation_ms / kRecordDotHalfPeriodMs) % 2 == 0;
	}
	else {
		const uint32_t phase = animation_ms % kThresholdBlinkCycleMs;
		m.blink_on = phase < kThresholdLitUntilMs || phase >= kThresholdLitFromMs;
	}
	return {.mode = views::oled::RecorderStatus::Mode::Meter, .word = {}, .meter = m};
}

views::grid::BlinkPads RecorderScene::present_blink(uint32_t now_ms) const {
	const uint32_t step = widgets::grid::blink_step(blink_anchor_ms_.value_or(now_ms), now_ms);
	return {.plan = widgets::grid::exclusive_blink(static_cast<uint8_t>(std::max<int32_t>(ctx_.source_index, 0)),
	                                               kRecordShortcutY, 0),
	        .step = step,
	        .fire = step != blink_fired_step_};
}

} // namespace deluge::ui::scenes::recorder
