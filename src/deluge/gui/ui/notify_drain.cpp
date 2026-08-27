#include "gui/ui/notify_drain.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include "util/notify.h"

/// Notice::popup_type is a plain uint8_t whose 0 means "unspecified" -- and today PopupType::NONE
/// happens to be 0 while GENERAL is 1, so a raw cast would turn every default notice into NONE.
/// Map 0 to GENERAL at the boundary; the cast disappears when PopupType moves into notify.
static PopupType popup_type_of(const deluge::notify::Notice& n) {
	return n.popup_type ? static_cast<PopupType>(n.popup_type) : PopupType::GENERAL;
}

void drain_notifications() {
	using namespace deluge::notify;
	const DirtySet d = dirty().drain();
	if (d.any()) {
		const LegacyRedraw r = legacy_redraw_for(d);
		if (RootUI* root = getRootUI(); root && (r.main_rows || r.sidebar_rows)) {
			uiNeedsRendering(root, r.main_rows ? 0xFFFFFFFF : 0, r.sidebar_rows ? 0xFFFFFFFF : 0);
		}
		if (r.oled) {
			renderUIsForOled();
		}
		// Legacy PlaybackHandler::endPlayback() cleared the tick squares inline; it now raises the
		// fact and we do it here, once playbackState says playback really has stopped.
		if (d.has(Dirty::PlaybackState) && !playbackHandler.playbackState) {
			PadLEDs::clearTickSquares();
		}
	}

	// The count-in popup. The tick tree only moves currentVisualCountForCountIn and raises
	// PlaybackState; the digit is formatted and shown here, in task context.
	static int32_t shown_count_in = 0;
	const int32_t count_in = playbackHandler.count_in_visual();
	if (count_in != shown_count_in) {
		if (count_in > 0) {
			char buffer[12];
			intToString(count_in, buffer);
			display->displayPopup(buffer, 0, true, 255, 2);
		}
		else {
			display->cancelPopup();
			// Legacy did `currentUIMode &= UI_MODE_AUDITIONING` unconditionally here; only the
			// count-in mode was ever meaningfully set during a count-in, so narrow it to that.
			if (currentUIMode == UI_MODE_RECORD_COUNT_IN) {
				currentUIMode &= UI_MODE_AUDITIONING;
			}
		}
		shown_count_in = count_in;
	}

	// Voice culling asked for the Play LED alert. Same "don't restart an existing blink" guard the
	// audio path used to apply itself.
	if (AudioEngine::cull_alert_pending) {
		AudioEngine::cull_alert_pending = false;
		if (indicator_leds::getLedBlinkerIndex(IndicatorLED::PLAY) == 255) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::PLAY);
		}
	}

	Notice n;
	while (notices().pop(n)) {
		switch (n.kind) {
		case Notice::Kind::Error:
			display->displayError(error_of(n));
			break;
		case Notice::Kind::Text:
			if (n.string_id) {
				display->displayPopup(deluge::l10n::get(string_of(n)), 3, false, 255, 1, popup_type_of(n));
			}
			else {
				display->displayPopup(n.text, 3, false, 255, 1, popup_type_of(n));
			}
			break;
		case Notice::Kind::Console:
			display->consoleText(n.string_id ? deluge::l10n::get(string_of(n)) : n.text);
			break;
		case Notice::Kind::Working:
			display->displayLoadingAnimationText(n.text);
			break;
		case Notice::Kind::ClearWorking:
			display->removeLoadingAnimation();
			display->removeWorkingAnimation();
			break;
		case Notice::Kind::CancelPopup:
			display->cancelPopup();
			break;
		}
	}
}
