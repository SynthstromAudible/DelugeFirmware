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
		RootUI* root = getRootUI();

		// Legacy PlaybackHandler::setupPlayback()/endPlayback() called RootUI::notifyPlaybackBegun()/
		// playbackEnded() directly and synchronously on the playbackState transition. Those overrides
		// mutate state beyond redrawing (ArrangerView::notifyPlaybackBegun() sets
		// mustRedrawTickSquares and can end a held row audition; ArrangerView::playbackEnded()
		// auto-scrolls; InstrumentClipView/AutomationView re-run reassessAllAuditionStatus()), so
		// raise(Dirty::PlaybackState) alone can't reproduce them -- re-dispatch on the edge here,
		// before any redraw below runs.
		if (d.has(Dirty::PlaybackState)) {
			static bool was_playing = false;
			const bool playing = playbackHandler.playbackState != 0;
			if (playing != was_playing) {
				if (playing) {
					// Legacy setupPlayback() only told the root UI when it was the UI on screen -- with
					// the exception of the note / note-row editor, so note edits stay audible.
					const bool in_note_editor =
					    getCurrentUI() == &soundEditor && (soundEditor.inNoteEditor() || soundEditor.inNoteRowEditor());
					if (root && (getCurrentUI() == root || in_note_editor)) {
						root->notifyPlaybackBegun();
					}
				}
				else if (currentSong && root) { // legacy endPlayback() guarded playbackEnded() the same way
					root->playbackEnded();
				}
				was_playing = playing;
			}
		}
		// Legacy Arrangement playback called ArrangerView::notifyActiveClipChangedOnOutput(output)
		// directly whenever a clip instance's active clip changed while a row audition was held, to
		// keep the shown mod-controllable target in sync. raise(Dirty::OutputChanged) carries no
		// output identity, so re-read the held row's own output instead of being told which output
		// changed (see ArrangerView::refresh_held_row_mod_target()).
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
			// Must be the *masked* compare: UI_MODE_RECORD_COUNT_IN is exclusive (45) while
			// UI_MODE_AUDITIONING is the (1 << 30) flag ORed on top, so during an audition-pad or
			// keyboard take currentUIMode is 0x4000002D and an exact compare would leave the
			// count-in mode set -- which is precisely the case the legacy mask existed for.
			if (isUIModeActive(UI_MODE_RECORD_COUNT_IN)) {
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
		case Notice::Kind::Text: {
			// `numFlashes` is the one non-7SEG parameter of displayPopup(): OLED::displayPopup() passes
			// `!numFlashes` as `persistent`, so 0 means "stays until something removes it" (the count-in
			// popup above uses exactly that). Severity::Warn is how a notice asks for it; everything
			// else keeps the legacy default of 3.
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
			display->removeLoadingAnimation();
			display->removeWorkingAnimation();
			break;
		case Notice::Kind::CancelPopup:
			display->cancelPopup();
			break;
		}
	}
}
