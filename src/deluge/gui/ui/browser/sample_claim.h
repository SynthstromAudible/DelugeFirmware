/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "model/sample/pitch_scan.h"
#include "storage/audio/latest_sample_load.h"

#include <cstdint>
#include <optional>
#include <string>

class AudioFile;
class AudioFileHolder;
class Sample;

namespace deluge::gui {

/// @brief Which gesture asked for a claim.
enum class ClaimOrigin : uint8_t {
	Explicit, ///< Enter, or the synth context menu: browser input is refused until the claim ends.
	AutoLoad, ///< The Load button's auto-load, as a preview lands: not gated, and the next cursor move cancels it.
};

/// @brief A claim, snapshotted when it is asked for.
struct ClaimRequest {
	std::string path;        ///< The file to claim, read once from the browser.
	std::string enteredText; ///< The browser's entered text when asked for: a kit drum is named after it.
	/// @name For each, 0 means no, 1 means auto, and 2 means definitely, as the user has asked for it.
	/// @{
	int32_t mayDoPitchDetection = 1;
	int32_t mayDoSingleCycle = 1;
	int32_t mayDoWaveTable = 1;
	/// @}
	bool loadWithoutExiting = false; ///< Keep the browser open once the file is claimed (auto-load).
	ClaimOrigin origin = ClaimOrigin::Explicit;
	void (*completion)(bool success) = nullptr; ///< Told how the claim ended. A cancelled claim tells it nothing.
};

/// @brief The sample browser's claim: loading the file the browser is on into the current synth, kit drum or audio
///        clip.
///
/// A phase machine on the interaction tier, advanced by request() and by routine() from its own repeating task, so it
/// runs whichever UI is on top. The file's load goes through the async sample load, and pitch detection is a PitchScan
/// ticked here, which never waits for the card. A wavetable's lookup still reads the card synchronously: a blocking
/// lookup on this tier, as song load's. Every model and UI change happens on the interaction tier.
///
/// An explicit claim closes the browser's gate (UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES) until it ends. An auto-load
/// claim does not, and the next cursor move cancels it. A cancelled claim ends at once: nothing of it is out anywhere
/// else.
class SampleClaim {
public:
	/// @brief Ask for a claim.
	///
	/// An auto-load claim asked for while an explicit claim is in flight is dropped. Any other request cancels a claim
	/// in flight. An explicit request closes the gate at once. Runs one pass.
	void request(ClaimRequest request);

	/// @brief Cancel an auto-load claim in flight, or waiting to start. An explicit claim is left as it is.
	void cancelAutoLoad();

	/// @brief Advance the claim as far as it can go without waiting. Called by its repeating task.
	void routine();

	/// @brief Whether a claim is in flight or waiting to start.
	[[nodiscard]] bool inFlight() const { return phase_ != Phase::Idle || queued_.has_value(); }

	/// @brief Whether an explicit claim is in flight or waiting to start: browser input is refused while it is.
	[[nodiscard]] bool explicitInFlight() const;

	/// @name Counts for the sample-claim gate.
	/// @{
	/// @brief Claims whose load stopped moving (AsyncSampleLoad's stall bound).
	[[nodiscard]] int32_t giveUps() const { return giveUps_; }
	/// @brief Claims cancelled.
	[[nodiscard]] int32_t cancels() const { return cancels_; }
	/// @brief Claims that ended claimed.
	[[nodiscard]] int32_t finishes() const { return finishes_; }
	/// @brief Ticks a pitch detection spent waiting for a cluster.
	[[nodiscard]] int32_t pitchScanTicks() const { return pitchScanTicks_; }
	/// @brief Pitch detections that gave up: no frame arrived for kMaxUnproductiveUs.
	[[nodiscard]] int32_t pitchGiveUps() const { return pitchGiveUps_; }
	/// @brief Whether the claim in flight is waiting for its pitch detection.
	[[nodiscard]] bool pitchPending() const { return phase_ == Phase::AwaitPitch; }
	/// @}

private:
	enum class Phase : uint8_t {
		Idle,             ///< No claim.
		Start,            ///< Refuse a recording audio clip; show the animation; work out the first type to try.
		PrepareSample,    ///< Cut the sound, unbind, set the path, and request the async load.
		LoadSample,       ///< Tick the async load.
		DecideSample,     ///< Act on the bound sample: redirect to a wavetable, name a drum, or go to pitch detection.
		PrepareWavetable, ///< Cut, unbind, set the path, and look the wavetable up (blocking, on this tier).
		DecideWavetable,  ///< Act on the lookup's result.
		DetectPitch,      ///< Start pitch detection.
		AwaitPitch,       ///< Tick pitch detection, then apply its note.
		Finish,           ///< The claim's tail: edits, the browser's exit, the animation.
	};

	/// @brief One step of the transition function.
	/// @return Whether to step again in this pass; false when the claim has to wait.
	bool step();
	bool stepStart();
	bool stepPrepareSample();
	bool stepLoadSample();
	bool stepDecideSample();
	bool stepPrepareWavetable();
	bool stepDecideWavetable();
	bool stepDetectPitch();
	bool stepAwaitPitch();
	bool stepFinish();

	/// @brief End the claim: remove the animation and show @p error unless it succeeded, release the gate, and tell the
	///        completion.
	void end(bool success, Error error);
	/// @brief Cancel the claim in flight: end it at once, telling no one.
	void cancel();
	/// @brief Remove the wavetable gold-knob assignments from a claim that ended as a sample.
	void removeWaveTableKnobs();

	Phase phase_{Phase::Idle};
	ClaimRequest current_{};
	std::optional<ClaimRequest> queued_{};
	/// @brief The file's load, for a sample: its payload is unused.
	deluge::storage::LatestSampleLoad<uint32_t> load_{};

	bool animationShown_{false};
	bool makeWaveTable_{false};
	int32_t numTypesTried_{0};
	bool doingSingleCycle_{false};
	OscType oldOscType_{OscType::SQUARE};

	/// @name The wavetable lookup's result.
	/// @{
	AudioFileHolder* waveTableHolder_{nullptr};
	AudioFile* waveTableFile_{nullptr};
	Error waveTableError_{Error::NONE};
	/// @}

	/// @name The detection in progress.
	/// @{
	deluge::sample::PitchScan pitchScan_{};
	bool pitchWanted_{false};
	Sample* pitchSample_{nullptr};
	/// @}

	int32_t giveUps_{0};
	int32_t cancels_{0};
	int32_t finishes_{0};
	int32_t pitchScanTicks_{0};
	int32_t pitchGiveUps_{0};
};

extern SampleClaim sampleClaim;

} // namespace deluge::gui
