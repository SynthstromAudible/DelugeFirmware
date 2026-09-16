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
#include "storage/audio/async_sample_load.h"
#include "storage/latest_wins.h"
#include "storage/storage_manager.h"

#include <cstdint>
#include <optional>
#include <string>

class Instrument;
class SoundDrum;

namespace deluge::gui {

/// @brief The file a preset load is for, snapshotted from the browser when it is asked for.
///
/// Plain values and Song-owned pointers only: the browser's list is rebuilt by listings while a load is out, so nothing
/// here may point into it.
struct InstrumentLoadTarget {
	bool loadingSynthToKitRow = false;
	int32_t movementDirection = 1; ///< Scroll-animation hint; unused by the load.
	bool hasFile = false;          ///< Whether the browser had a current file.
	bool isFolder = false;
	bool maybeExistsOnCard = true;
	/// The file's already-loaded (possibly hibernating) Instrument, if any. Song-owned, so it outlives a relisting.
	Instrument* existingInstrument = nullptr;
	std::string path;    ///< The file's full path.
	std::string name;    ///< The browser's entered text: the loaded Instrument's (or drum's) name.
	std::string dirPath; ///< The browser's folder.
};

/// @brief One preset load, as the browser asks for it.
struct InstrumentLoadRequest {
	InstrumentLoadTarget target{};
	bool clone = false;  ///< Load a new copy under an unused variation of the name, even of the current Instrument.
	bool commit = false; ///< The browser's Select or the clone menu: the gate is closed until it ends.
	/// Told how the load ended. A superseded or cancelled load tells it nothing.
	void (*completion)(Error error) = nullptr;
};

/// @brief What the browser decided about a load before any card work: done already, reuse an Instrument, or read one.
struct InstrumentLoadDecision {
	Error error = Error::NONE;
	bool done = false;               ///< Nothing to load: end with @ref error.
	Instrument* reuse = nullptr;     ///< An existing or hibernating Instrument to swap in without reading a file.
	bool wasHibernating = false;     ///< @ref reuse came off the hibernation list.
	bool shouldReplaceWhole = false; ///< Replace the whole Instrument rather than change one Clip's.
	bool needToAdd = false;          ///< The swapped-in Instrument must be added to the song.
	std::string clonedName;          ///< A clone's name.
};

/// @brief The preset browser's own loads: a synth, kit or MIDI preset into the browser's clip, a synth preset into a
///        kit row, or a clone.
///
/// A phase machine on the interaction tier, advanced by request() and by routine() from its own repeating task. The
/// preset's document is staged through the byte channel and parsed on this tier; its samples load through one
/// AsyncSampleLoad over the new Instrument (or drum); the browser swaps it in, in one pass; a MIDI Instrument's device
/// definition is staged and parsed the same way. Nothing runs on the storage owner.
///
/// Requests are coalesced latest-wins: a newer request cancels the load in flight at its next phase boundary
/// (staging or samples) and starts. An Instrument parsed but not yet swapped in is deleted, never having been added to
/// the song. A commit asked for the file a scroll-load is already loading promotes that load instead of restarting it.
/// A commit or clone closes UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES until it ends, released only while still that mode;
/// a scroll-load never closes it. A sample load that fails or stops moving keeps the Instrument: it is swapped in with
/// the unanswered holders empty, and the card error is shown once.
class InstrumentLoad {
public:
	/// @brief Ask for a load, and run one pass.
	void request(const InstrumentLoadRequest& request);

	/// @brief Abandon the load in flight and anything queued behind it, telling no one.
	void cancel();

	/// @brief Advance the load as far as it can go without waiting. Called by the "instrument load" repeating task.
	void routine();

	/// @brief Whether a load is in flight or queued.
	[[nodiscard]] bool inFlight() const { return phase_ != Phase::Idle; }

	/// @brief Whether a commit or clone is in flight: the browser refuses its input until it ends.
	[[nodiscard]] bool commitInFlight() const { return phase_ != Phase::Idle && active_.commit; }

	/// @name Counts for the preset-load gate.
	/// @{
	/// Preset documents (and device definitions) staged.
	[[nodiscard]] int32_t documentReads() const { return documentReads_; }
	/// Of those, the ones begun on the storage owner; required 0.
	[[nodiscard]] int32_t documentReadsOnOwner() const { return documentReadsOnOwner_; }
	/// Sample loads begun, one per Instrument or drum.
	[[nodiscard]] int32_t sampleLoads() const { return sampleLoads_; }
	/// Of those, the ones begun on the storage owner; required 0.
	[[nodiscard]] int32_t sampleLoadsOnOwner() const { return sampleLoadsOnOwner_; }
	/// Loads cancelled by a newer request.
	[[nodiscard]] int32_t supersessions() const { return supersessions_; }
	/// Sample loads that stopped moving (AsyncSampleLoad's stall bound).
	[[nodiscard]] int32_t giveUps() const { return giveUps_; }
	/// @}

private:
	enum class Phase : uint8_t {
		Idle,    ///< No load.
		Decide,  ///< The browser's card-free prologue.
		Stage,   ///< Staging the preset document, a tick at a time.
		Parse,   ///< Reading the staged document into a new Instrument or drum.
		Samples, ///< Ticking the new Instrument's (or drum's) sample load.
		Swap,    ///< The browser swaps it in, in one pass.
		MidiDef, ///< Staging and reading a MIDI Instrument's device definition.
	};

	bool step();
	bool stepDecide();
	bool stepStage();
	bool stepParse();
	bool stepSamples();
	bool stepSwap();
	bool stepMidiDef();

	/// @brief Start @p request's load from its prologue.
	void start(const InstrumentLoadRequest& request);
	/// @brief Undo what the load in flight holds that the song does not: its staging, its sample load, and an
	/// Instrument
	///        or drum parsed but not swapped in.
	void abandon();
	/// @brief End the load with @p error, tell its completion, and start what was queued behind it.
	/// @return Whether a queued load was started, and so whether to step again.
	bool finish(Error error);
	/// @brief Stage @p path as a @p firstTagName document; restores `activeDeserializer` on a failure.
	StorageManager::StagedOpen stage(const char* path, const char* firstTagName, const char* altTagName);
	/// @brief Free the staging, and put back `activeDeserializer` as it was before the staging opened.
	void releaseStaging();

	Phase phase_{Phase::Idle};
	deluge::storage::LatestWins<InstrumentLoadRequest> coalescer_{};
	InstrumentLoadRequest active_{};
	InstrumentLoadDecision decision_{};

	/// @name What the load in flight holds.
	/// @{
	StorageManager::StagingState staging_{};
	bool stagingOpen_{false}; ///< A staging has begun and not been released.
	FileDeserializer* previousDeserializer_{nullptr};
	Instrument* newInstrument_{nullptr};
	SoundDrum* newDrum_{nullptr};
	Instrument* swapped_{nullptr}; ///< The Instrument swapped in, for the MIDI definition.
	std::optional<deluge::storage::AsyncSampleLoad> load_{};
	/// @}

	int32_t documentReads_{0};
	int32_t documentReadsOnOwner_{0};
	int32_t sampleLoads_{0};
	int32_t sampleLoadsOnOwner_{0};
	int32_t supersessions_{0};
	int32_t giveUps_{0};
};

extern InstrumentLoad instrumentLoad;

} // namespace deluge::gui
