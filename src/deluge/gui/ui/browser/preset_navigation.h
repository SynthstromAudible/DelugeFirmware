#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"

class Song;
class UI;
class Clip;
class Instrument;

namespace deluge::gui {

// UI-independent state shared by the deferred loader and its original caller.
enum class PresetNavigationDestination { CLIP, ARRANGER, OUTPUT };
enum class PresetNavigationDecision { CANCEL, WAIT, LOAD };
enum class PresetInputAction { PASS, DEFER, RESOLVE };
[[nodiscard]] PresetInputAction presetInputAction(bool pending, bool press, bool cardBusy, bool parsing);

// Keeps accumulated encoder detents across catalog pages and wrap boundaries.
class PresetNavigationSteps {
public:
	explicit PresetNavigationSteps(int32_t offset);
	[[nodiscard]] int32_t direction() const { return direction_; }
	[[nodiscard]] bool finished() const { return remaining_ == 0; }
	bool visit(bool available);

private:
	int32_t direction_;
	uint32_t remaining_;
};

struct PresetNavigationContext {
	Song* song = nullptr;
	UI* ui = nullptr;
	uint32_t uiMode = UI_MODE_NONE;
	Clip* currentClip = nullptr;
	Clip* selectedClip = nullptr;
	Clip* displayClip = nullptr;
	bool fromArranger = false;
	bool replacingWholeOutput = false;
	bool padReleased = false;

	bool matchesUI(Song* liveSong, UI* liveUI, uint32_t liveMode, Clip* liveCurrentClip) const;
	PresetNavigationDecision decision(Song* liveSong, UI* liveUI, uint32_t liveMode, Clip* liveCurrentClip,
	                                  bool arrangementRecording) const;
	bool matchesSelection(Clip* liveSelectedClip, bool arrangerTargetMatches) const;
	void markPadReleased(bool targetMatches);
	[[nodiscard]] PresetNavigationDestination destination() const;
};

// Both catalog filtering and navigation must allow returning to the origin,
// without making any other occupied instrument available.
bool presetInstrumentAvailable(Availability requirement, bool alreadyInSong, bool activeInSession,
                               const Instrument* candidate, const Instrument* exemptInstrument = nullptr);

class PresetLoadGuard {
public:
	PresetLoadGuard(bool& previewPending, uint32_t& uiMode, bool& interrupted);
	~PresetLoadGuard();
	PresetLoadGuard(const PresetLoadGuard&) = delete;
	PresetLoadGuard& operator=(const PresetLoadGuard&) = delete;

private:
	uint32_t& uiMode_;
	uint32_t previousMode_;
};

// Timer cancellation and file loading remain platform operations. The busy mode
// covers both callbacks, and the result is recorded before restoring the mode.
template <typename CancelTimer, typename Load>
void performPresetLoad(bool& pending, uint32_t& uiMode, bool& interrupted, Error& result, CancelTimer cancelTimer,
                       Load load) {
	PresetLoadGuard guard(pending, uiMode, interrupted);
	cancelTimer();
	result = load();
}

} // namespace deluge::gui
