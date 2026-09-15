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
/// runs whichever UI is on top. The file's load goes through the async sample load. The two steps that still read the
/// card synchronously run as minimal operations on the storage owner, each returning a result and changing no model: a
/// wavetable's lookup, and pitch detection's scan. Every model and UI change happens on the interaction tier.
///
/// An explicit claim closes the browser's gate (UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES) until it ends. An auto-load
/// claim does not, and the next cursor move cancels it. A claim cancelled while an operation is out drains: nothing
/// touches its holder until the operation reports, and a claim asked for meanwhile waits.
class SampleClaim {
public:
	/// @brief Ask for a claim.
	///
	/// An auto-load claim asked for while an explicit claim is in flight is dropped. Any other request cancels a claim
	/// in flight, and waits for its drain if it has one. An explicit request closes the gate at once. Runs one pass.
	void request(ClaimRequest request);

	/// @brief Cancel an auto-load claim in flight, or waiting to start. An explicit claim is left as it is.
	void cancelAutoLoad();

	/// @brief Advance the claim as far as it can go without waiting. Called by its repeating task.
	void routine();

	/// @brief Whether a claim is in flight, draining, or waiting to start.
	[[nodiscard]] bool inFlight() const { return phase_ != Phase::Idle || queued_.has_value(); }

	/// @brief Whether an explicit claim is in flight or waiting to start: browser input is refused while it is.
	[[nodiscard]] bool explicitInFlight() const;

	/// @brief Whether an operation queued on the storage owner has not finished running, whichever claim queued it.
	[[nodiscard]] bool opOutstanding() const { return opRunning_; }

	/// @name Counts for the sample-claim gate.
	/// @{
	/// Claims that began on the storage owner; required 0.
	[[nodiscard]] int32_t loadsOnOwner() const { return loadsOnOwner_; }
	/// Wavetable lookups that ran on the storage owner.
	[[nodiscard]] int32_t waveTableLookupsOnOwner() const { return waveTableLookupsOnOwner_; }
	/// Pitch detections that ran on the storage owner.
	[[nodiscard]] int32_t pitchScansOnOwner() const { return pitchScansOnOwner_; }
	/// Claims whose load stopped moving (AsyncSampleLoad's stall bound).
	[[nodiscard]] int32_t giveUps() const { return giveUps_; }
	/// Claims cancelled.
	[[nodiscard]] int32_t cancels() const { return cancels_; }
	/// Cancelled claims that waited for an operation to report.
	[[nodiscard]] int32_t drains() const { return drains_; }
	/// Claims started while an operation was still out; required 0.
	[[nodiscard]] int32_t startsUnderOp() const { return startsUnderOp_; }
	/// Claims that ended claimed.
	[[nodiscard]] int32_t finishes() const { return finishes_; }
	/// @}

private:
	enum class Phase : uint8_t {
		Idle,             ///< No claim.
		Start,            ///< Refuse a recording audio clip; show the animation; work out the first type to try.
		PrepareSample,    ///< Cut the sound, unbind, set the path, and request the async load.
		LoadSample,       ///< Tick the async load.
		DecideSample,     ///< Act on the bound sample: redirect to a wavetable, name a drum, or go to pitch detection.
		PrepareWavetable, ///< Cut, unbind, set the path, and dispatch the wavetable lookup.
		AwaitWavetable,   ///< Wait for the wavetable lookup's result.
		DetectPitch,      ///< Dispatch pitch detection.
		AwaitPitch,       ///< Wait for pitch detection, then apply its note.
		Finish,           ///< The claim's tail: edits, the browser's exit, the animation.
		Draining,         ///< A cancelled claim waiting for its operation to report.
	};

	/// @brief One operation's result slot: written by the operation on the owner, read by the next pass.
	struct OpResult {
		bool done = false;
		AudioFile* file = nullptr;
		Error error = Error::NONE;
	};

	/// @brief One step of the transition function.
	/// @return Whether to step again in this pass; false when the claim has to wait.
	bool step();
	bool stepStart();
	bool stepPrepareSample();
	bool stepLoadSample();
	bool stepDecideSample();
	bool stepPrepareWavetable();
	bool stepAwaitWavetable();
	bool stepDetectPitch();
	bool stepAwaitPitch();
	bool stepFinish();

	/// @brief End the claim: remove the animation and show @p error unless it succeeded, release the gate, and tell the
	///        completion.
	void end(bool success, Error error);
	/// @brief Cancel the claim in flight: drain if an operation is out, otherwise end it at once, telling no one.
	void cancel();
	/// @brief Queue an operation on the storage owner, or leave it for the next pass if the queue is full.
	void dispatchOp();
	/// @brief Remove the wavetable gold-knob assignments from a claim that ended as a sample.
	void removeWaveTableKnobs();

	static void runWaveTableOp(void* self);
	static void runPitchOp(void* self);

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

	/// @name The operation out, and what it works on.
	/// @{
	void (*opFn_)(void*) = nullptr;
	bool opDispatched_{false};
	/// Set as an operation is queued, and cleared by the operation as it ends: what a drain waits for, and what no
	/// claim may start under.
	bool opRunning_{false};
	OpResult op_{};
	AudioFileHolder* opHolder_{nullptr};
	bool opMakeWaveTable_{false};
	Sample* opSample_{nullptr};
	bool opSingleCycle_{false};
	/// @}

	int32_t loadsOnOwner_{0};
	int32_t waveTableLookupsOnOwner_{0};
	int32_t pitchScansOnOwner_{0};
	int32_t giveUps_{0};
	int32_t cancels_{0};
	int32_t drains_{0};
	int32_t startsUnderOp_{0};
	int32_t finishes_{0};
};

extern SampleClaim sampleClaim;

} // namespace deluge::gui
