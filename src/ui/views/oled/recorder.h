#pragma once
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/threshold_meter.h"

#include <cstdint>
#include <string_view>

/// @file
/// @brief The audio recorder's screen: its status word while armed, its threshold meter while waiting or recording.

namespace deluge::ui::views::oled {

/// @brief The row the status word draws on, in frame-local coordinates.
///
/// `AudioRecorder::renderOLED` is one call: `drawStringCentred(text, 19, kTextBigSpacingX, kTextBigSizeY)`. That 19 is
/// an absolute canvas row and the frame's origin already accounts for the bezel, so the frame-local row is
/// `19 - OLED_MAIN_TOPMOST_PIXEL` = 14 on the shipped panel. On a panel with no bezel the word sits 14 rows from the
/// top of the frame.
inline constexpr int32_t kStatusY = 14;

/// @brief What the recorder screen shows.
struct RecorderStatus {
	enum class Mode : uint8_t {
		Word,  ///< `word`, centred.
		Meter, ///< `meter`.
	} mode;
	std::string_view word;               ///< "Waiting" / "Recording".
	widgets::oled::ThresholdMeter meter; ///< The threshold screen's state.
};

/// @brief `AudioRecorder::renderOLED`: the word in MetricBold13 centred at `kStatusY`, or `threshold_meter`.
void recorder_status(widgets::oled::OledFrame& f, const RecorderStatus& d);

} // namespace deluge::ui::views::oled
