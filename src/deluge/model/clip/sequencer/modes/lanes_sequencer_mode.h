/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "gui/l10n/l10n.h"
#include "model/clip/sequencer/sequencer_mode.h"
#include "model/sequencing/lanes_engine.h"
#include <memory>

class Serializer;
class Deserializer;

namespace deluge::model::clip::sequencer::modes {

/**
 * Lanes Sequencer Mode — generative polymetric sequencing with independent parameter lanes.
 *
 * Each clip gets 8 independent parameter lanes displayed on the grid (one per row).
 * Each lane can have its own length, direction, and clock division. When lanes have
 * different lengths they phase against each other (polymetric sequencing).
 *
 * Wraps LanesEngine (pure model) and implements the SequencerMode interface for
 * rendering, input handling, playback, and persistence.
 */
class LanesSequencerMode : public SequencerMode {
public:
	LanesSequencerMode();
	~LanesSequencerMode() override = default;

	// === Core identification ===
	l10n::String name() override { return l10n::String::STRING_FOR_LANES_SEQ; }

	// === Compatibility ===
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }
	bool supportsMIDI() override { return true; }
	bool supportsCV() override { return true; }
	bool supportsAudio() override { return false; }

	// === Lifecycle ===
	void initialize() override;
	void cleanup() override;

	// === Rendering ===
	bool renderPads(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth) override;

	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;

	// === Input handling ===
	bool handlePadPress(int32_t x, int32_t y, int32_t velocity) override;
	bool handleHorizontalEncoder(int32_t offset, bool encoderPressed) override;

	// === Playback ===
	int32_t processPlayback(void* modelStack, int32_t absolutePlaybackPos) override;
	void stopAllNotes(void* modelStack) override;

	// === Persistence ===
	void writeToFile(Serializer& writer, bool includeScenes = true) override;
	Error readFromFile(Deserializer& reader) override;
	bool copyFrom(SequencerMode* other) override;

	// === Generative mutations ===
	void resetToInit() override;
	void randomizeAll(int32_t mutationRate = 100) override;
	void evolveNotes(int32_t mutationRate = 30) override;

	// === Scene management ===
	size_t captureScene(void* buffer, size_t maxSize) override;
	bool recallScene(const void* buffer, size_t size) override;

	// === Direct engine access (for menu items and undo) ===
	LanesEngine* getEngine() { return engine_.get(); }
	const LanesEngine* getEngine() const { return engine_.get(); }

	// === Select encoder (for InstrumentClipView delegation) ===
	bool handleSelectEncoder(int32_t offset);

	// === Performance mode (sidebar toggle) ===
	bool isPerformanceMode() const { return performanceMode_; }
	void togglePerformanceMode();

protected:
	bool handleModeSpecificVerticalEncoder(int32_t offset) override;

private:
	std::unique_ptr<LanesEngine> engine_;

	// === Playback state ===
	int16_t currentNote_{-1};
	int32_t gateTicksRemaining_{0};
	bool tieActive_{false};
	uint8_t retriggersRemaining_{0};
	int32_t retriggerSubTicks_{0};
	uint8_t retriggerVelocity_{100};
	bool retriggerGapPhase_{false};
	int32_t lastAbsolutePos_{0};

	// === Sidebar mode ===
	bool performanceMode_{false};

	// === Helpers ===
	static int32_t lanesRowToLaneIdx(int32_t yDisplay);
	static RGB laneColor(int32_t laneIdx);
	bool enterLaneEditor();

	void stopCurrentNote(void* modelStack);
	void resetPlaybackState();
};

/// Utility: get the LanesEngine from the current clip's sequencer mode.
/// Returns nullptr if the clip isn't in lanes mode.
LanesEngine* getCurrentLanesEngine();

} // namespace deluge::model::clip::sequencer::modes
