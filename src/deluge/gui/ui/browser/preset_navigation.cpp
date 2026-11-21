#include "gui/ui/browser/preset_navigation.h"

namespace deluge::gui {

PresetInputAction presetInputAction(bool pending, bool press, bool cardBusy, bool parsing, bool loading) {
	if (loading) {
		return PresetInputAction::DEFER;
	}
	if (!pending || !press) {
		return PresetInputAction::PASS;
	}
	return cardBusy || parsing ? PresetInputAction::DEFER : PresetInputAction::RESOLVE;
}

PresetNavigationSteps::PresetNavigationSteps(int32_t offset)
    : direction_(offset < 0 ? -1 : 1),
      remaining_(static_cast<uint32_t>(offset < 0 ? -static_cast<int64_t>(offset) : offset)) {
	if (remaining_ == 0) {
		remaining_ = 1;
	}
}

bool PresetNavigationSteps::visit(bool available) {
	if (available && remaining_ != 0) {
		--remaining_;
	}
	return finished();
}

PresetNavigationDecision PresetNavigationContext::decision(Song* liveSong, UI* liveUI, uint32_t liveMode,
                                                           Clip* liveCurrentClip, bool arrangementRecording) const {
	// The Session caller checks this before starting navigation. Recheck after
	// the delay too, including after release has cleared the live selection.
	if ((selectedClip != nullptr) && arrangementRecording) {
		return PresetNavigationDecision::CANCEL;
	}
	if (matchesUI(liveSong, liveUI, liveMode, liveCurrentClip)) {
		return PresetNavigationDecision::LOAD;
	}
	// Auditioning the same clip is temporary. Do not replace an instrument while
	// it is auditioned, or discard the selection just because the pad is held.
	if (song == liveSong && ui == liveUI && currentClip == liveCurrentClip && uiMode == UI_MODE_NONE
	    && (selectedClip == nullptr) && destination() == PresetNavigationDestination::CLIP
	    && liveMode == UI_MODE_AUDITIONING) {
		return PresetNavigationDecision::WAIT;
	}
	return PresetNavigationDecision::CANCEL;
}

bool PresetNavigationContext::matchesUI(Song* liveSong, UI* liveUI, uint32_t liveMode, Clip* liveCurrentClip) const {
	uint32_t expected_mode = padReleased ? UI_MODE_NONE : uiMode;
	return liveSong == song && liveUI == ui && liveMode == expected_mode && liveCurrentClip == currentClip;
}

bool PresetNavigationContext::matchesSelection(Clip* liveSelectedClip, bool arrangerTargetMatches) const {
	return (padReleased || (selectedClip == nullptr) || liveSelectedClip == selectedClip)
	       && (!fromArranger || arrangerTargetMatches);
}

void PresetNavigationContext::markPadReleased(bool targetMatches) {
	if (targetMatches
	    && (uiMode == UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION || uiMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {
		padReleased = true;
	}
}

PresetNavigationDestination PresetNavigationContext::destination() const {
	if ((displayClip != nullptr) && !replacingWholeOutput) {
		return PresetNavigationDestination::CLIP;
	}
	return fromArranger ? PresetNavigationDestination::ARRANGER : PresetNavigationDestination::OUTPUT;
}

bool presetInstrumentAvailable(Availability requirement, bool alreadyInSong, bool activeInSession,
                               const Instrument* candidate, const Instrument* exemptInstrument) {
	if (!alreadyInSong || ((candidate != nullptr) && candidate == exemptInstrument)) {
		return true;
	}
	if (requirement == Availability::INSTRUMENT_UNUSED) {
		return false;
	}
	return requirement != Availability::INSTRUMENT_AVAILABLE_IN_SESSION || !activeInSession;
}

PresetLoadGuard::PresetLoadGuard(bool& previewPending, uint32_t& uiMode, bool& interrupted)
    : uiMode_(uiMode), previousMode_(uiMode) {
	interrupted = false;
	previewPending = false;
	uiMode_ = UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED;
}

PresetLoadGuard::~PresetLoadGuard() {
	uiMode_ = previousMode_;
}

} // namespace deluge::gui
