#include "gui/ui/notify_drain.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/clip/audio_clip.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/c_string.h"
#include "util/note_editor_open.h"
#include "util/notify.h"
// Notice::popup_type is a plain uint8_t whose 0 means "unspecified" -- and today PopupType::NONE
// happens to be 0 while GENERAL is 1, so a raw cast would turn every default notice into NONE.
// Map 0 to GENERAL at this boundary.
static PopupType popup_type_of(const deluge::notify::Notice& n) {
	return n.popup_type ? static_cast<PopupType>(n.popup_type) : PopupType::GENERAL;
}

void drain_notifications() {
	using namespace deluge::notify;
	const DirtySet d = dirty().drain();
	if (d.any()) {
		const LegacyRedraw r = legacy_redraw_for(d);
		RootUI* root = getRootUI();

		// Re-dispatches the legacy `RootUI::notifyPlaybackBegun()`/`playbackEnded()` hooks, before any
		// redraw below runs: those overrides mutate state beyond redrawing (ArrangerView sets
		// mustRedrawTickSquares and can end a held row audition; ArrangerView::playbackEnded()
		// auto-scrolls; InstrumentClipView/AutomationView re-run reassessAllAuditionStatus()), so
		// raise(Dirty::PlaybackState) alone can't reproduce them.
		if (d.has(Dirty::PlaybackState)) {
			// Every playback start/stop raises PlaybackState unconditionally (playback_handler.cpp
			// setupPlayback()/endPlayback()), so this edge detector always sees the transition; the
			// on-screen guard is applied only here, on the re-dispatch below.
			static bool was_playing = false;
			const bool playing = playbackHandler.playbackState != 0;
			if (playing != was_playing) {
				if (playing) {
					// Only re-dispatch when the root UI is on screen, with the exception of the
					// note / note-row editor, so note edits stay audible.
					const bool in_note_editor =
					    deluge::gui::ui::noteEditorOpen() || deluge::gui::ui::noteRowEditorOpen();
					if (root && (getCurrentUI() == root || in_note_editor)) {
						root->notifyPlaybackBegun();
					}
				}
				else if (currentSong && root) { // same on-screen guard as playback start
					root->playbackEnded();
				}
				was_playing = playing;
			}
		}
		// Re-dispatches the legacy `ArrangerView::notifyActiveClipChangedOnOutput()` hook: keeps the
		// shown mod-controllable target in sync with the held row's active clip. Dirty::OutputChanged
		// carries no output identity, so re-read the held row's own output instead of being told which
		// output changed (see ArrangerView::refresh_held_row_mod_target()).
		if (d.has(Dirty::OutputChanged) && root == &arrangerView) {
			arrangerView.refresh_held_row_mod_target();
		}

		// AudioClipView::clipNeedsReRendering() is not a pure redraw request: when the clip's length
		// just shrank under the scroll position (an aborted or finished linear recording) it scrolls
		// back left instead of rendering. Route clip-content facts through it so that survives.
		// Testing the flag alone is enough: drain() derives it from the clip words it took, so the
		// flag is always a superset of the clip bits (dirty_set.h). The generic request below still
		// runs -- doAnyPendingGridRendering() sits out UI_MODE_HORIZONTAL_SCROLL, so a main-row bit
		// set during the scroll animation simply waits for it to finish.
		if (d.has(Dirty::ClipContent) && root == &audioClipView && currentSong) {
			if (AudioClip* c = getCurrentAudioClip()) {
				audioClipView.clipNeedsReRendering(c);
			}
		}
		if (root && (r.main_rows || r.sidebar_rows)) {
			uiNeedsRendering(root, r.main_rows ? 0xFFFFFFFF : 0, r.sidebar_rows ? 0xFFFFFFFF : 0);
		}
		if (r.oled) {
			renderUIsForOled();
		}
		// Clears the tick squares that legacy `PlaybackHandler::endPlayback()` used to clear inline,
		// once playbackState confirms playback has actually stopped.
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
			// Only clear the count-in mode -- it's the only mode meaningfully set during a count-in.
			// Must be the masked compare: UI_MODE_RECORD_COUNT_IN is exclusive (45) while
			// UI_MODE_AUDITIONING is the (1 << 30) flag ORed on top, so during an audition-pad or
			// keyboard take currentUIMode is 0x4000002D and an exact compare would leave the
			// count-in mode set.
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) {
				currentUIMode &= UI_MODE_AUDITIONING;
			}
		}
		shown_count_in = count_in;
	}

	// Voice culling asked for the Play LED alert; skip if a blink is already running (same guard
	// the audio path applies itself). exchange(false, relaxed), not a load then a store: the
	// audio render path can set the flag again between the two, and a plain store here would
	// silently drop that re-raise.
	if (AudioEngine::cull_alert_pending.exchange(false, std::memory_order_relaxed)) {
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
		case Notice::Kind::Text: {
			// `numFlashes` is the one non-7SEG parameter of displayPopup(): OLED::displayPopup() passes
			// `!numFlashes` as `persistent`, so 0 means "stays until something removes it" (the count-in
			// popup above uses exactly that). Severity::Warn is how a notice asks for it; everything
			// else keeps the default of 3.
			const int8_t flashes = (n.severity == Notice::Severity::Warn) ? 0 : 3;
			if (n.string_id) {
				display->displayPopup(deluge::l10n::get(string_of(n)), flashes, false, 255, 1, popup_type_of(n));
			}
			else {
				display->displayPopup(n.text, flashes, false, 255, 1, popup_type_of(n));
			}
			break;
		}
		case Notice::Kind::Console:
			display->consoleText(n.string_id ? deluge::l10n::get(string_of(n)) : n.text);
			break;
		case Notice::Kind::Working:
			display->displayLoadingAnimationText(n.text);
			break;
		case Notice::Kind::ClearWorking:
			// Display::removeLoadingAnimation() is removeWorkingAnimation() on the only implementation
			// there is (oled.h), so one call clears both spellings.
			display->removeWorkingAnimation();
			break;
		case Notice::Kind::CancelPopup:
			display->cancelPopup();
			break;
		case Notice::Kind::CancelConsole:
			// Legacy's own `HorizontalMenu::displayNotification` shows through
			// `OLED::displayNotification`, which sets the *same* `oledPopupWidth`/`popupType`
			// slot a framed popup does (`PopupType::NOTIFICATION`) -- so the ShellMenu-off leg's
			// cancel is the identical `display->cancelPopup()` call `Notice::Kind::CancelPopup`
			// above already makes; the port's two are only split because `ConsoleScene` and
			// `PopupScene` are separate scenes. Guarded exactly as legacy's own two call sites are
			// (`HorizontalMenu::endSession`/`switchVisiblePage`, `horizontal_menu.cpp:587-589`,
			// `:363-365`: `if (display->hasPopupOfType(PopupType::NOTIFICATION)) { cancelPopup(); }`)
			// -- this arm is dead while `MenuScene` runs the toggle-on leg, but the port of the
			// check belongs here rather than assumed away.
			if (display->hasPopupOfType(PopupType::NOTIFICATION)) {
				display->cancelPopup();
			}
			break;
		}
	}
}
