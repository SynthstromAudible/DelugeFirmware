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
#include "gui/ui/load/preset_walk.h"
#include "storage/audio/async_sample_load.h"
#include "storage/latest_wins.h"
#include "storage/preset_acquisition.h"
#include "storage/storage_manager.h"

#include <cstdint>
#include <optional>
#include <string>

class Instrument;
class MIDIInstrument;
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

/// @brief What the browser's exit reverts to: the preset it puts back, and how.
struct RevertTarget {
	std::string path;    ///< The original preset's full path.
	std::string name;    ///< Its name.
	std::string dirPath; ///< Its folder.
	OutputType outputType{OutputType::SYNTH};
	InstrumentClip* clip{};            ///< The clip the browser was opened for; null from the arranger.
	Instrument* instrumentToReplace{}; ///< What the browser left on the clip, to be replaced by the original.
	bool replaceWhole{};               ///< Replace the whole Instrument rather than change the clip's.
	bool needToAdd{};                  ///< The original must be added to the song.
};

/// @brief The MIDI device definition the definition browser asked for: the file, and the Instrument it labels.
struct MidiDefinitionTarget {
	std::string path;             ///< The definition file's full path.
	MIDIInstrument* instrument{}; ///< The Instrument to read it into. Song-owned, so it outlives a relisting.
};

/// @brief One preset load: the browser's own, an acquisition a gesture outside the browser asked for, the revert the
///        browser's exit leaves behind, or a MIDI device definition.
struct InstrumentLoadRequest {
	/// @brief Which of the two a request is.
	enum class Kind : uint8_t {
		Browse,         ///< The browser's scroll-load, commit or clone, of @ref target.
		Acquire,        ///< A change-type or navigate gesture's acquisition, of what @ref preset's walk finds.
		Revert,         ///< The browser's exit, putting @ref revert's original preset back.
		MidiDefinition, ///< The definition browser's Select, of @ref definition's file.
	};

	InstrumentLoadTarget target{};
	bool clone = false;  ///< Load a new copy under an unused variation of the name, even of the current Instrument.
	bool commit = false; ///< The browser's Select or the clone menu: the gate is closed until it ends.
	/// Told how a browser load ended. A superseded or cancelled load tells it nothing.
	void (*completion)(Error error) = nullptr;
	Kind kind = Kind::Browse;
	/// Acquire only: the gesture, handed to LoadInstrumentPresetUI's consumer follow-up when the Instrument is ready.
	deluge::storage::PresetRequest preset{};
	/// Revert only: the original to read, and the swap to make when it is ready.
	RevertTarget revert{};
	/// MidiDefinition only: the definition file, and the MIDI Instrument it is read into.
	MidiDefinitionTarget definition{};
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

/// @brief What an acquisition's walk found, copied off the browser's list on the storage owner.
struct AcquireSnapshot {
	Error error = Error::NONE;
	std::string path;    ///< The preset's full path.
	std::string name;    ///< The preset's (or existing Instrument's) name.
	std::string dirPath; ///< The folder the walk found it in.
	/// An Instrument the song already holds, active or hibernating, for the preset. Song-owned.
	Instrument* existing = nullptr;
	bool alreadyInSong = false; ///< @ref existing is active in the song, not hibernating.
};

/// @brief Preset loads on the interaction tier: the browser's own, and every change-type and navigate acquisition.
///
/// A phase machine, advanced by request() and by routine() from its own repeating task.
/// - **Browse** (a synth, kit or MIDI preset into the browser's clip, a synth preset into a kit row, or a clone):
///   the browser's prologue decides; the document is staged through the byte channel and parsed on this tier; the
///   samples load through one AsyncSampleLoad over the new Instrument (or drum); the browser swaps it in, in one pass.
/// - **Acquire** (a change-type or navigate gesture outside the browser): the gesture's card-free prologue runs as the
///   request starts; one operation on the storage owner walks the instrument folder, as today's walkers do, and
///   copies out what it found; a preset from the card is staged, parsed and has its samples loaded as above, a
///   hibernating Instrument has its samples loaded, and an active one is taken as it is; the gesture's follow-up is
///   then handed the Instrument, and applies it.
///
/// - **Revert** (the browser's exit, when the clip's original preset has left memory): staged, parsed and loaded as
///   above, then swapped back by the browser's shared revert tail. The browser has already closed; the gate stays
///   closed until it lands.
///
/// - **MidiDefinition** (the definition browser's Select): the file is staged and read into the MIDI Instrument the
///   browser was opened on, and the browser is told how it ended -- it closes on success, and stays open on a card
///   error.
///
/// Requests share one latest-wins queue: a newer request cancels the load in flight at its next phase
/// boundary (staging, samples, or a landed walk) and starts -- except a creation (a new instrument, new track or
/// clone), a revert and a device definition: one in flight is never cancelled, a newer request waiting behind it, and
/// one queued is never replaced, what comes after it being refused. An Instrument parsed but not yet handed over is
/// deleted, never having been added to the song. A navigate that supersedes a navigate walks on from where the
/// superseded one's walk landed. A commit asked for the file a scroll-load is already loading promotes that load
/// instead of restarting it. A browser commit or clone closes UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES until it ends,
/// released only while still that mode; a scroll-load or an acquisition never closes it. A sample load that fails or
/// stops moving keeps the Instrument: it is swapped in or handed over with the unanswered holders empty, and the card
/// error is shown once.
class InstrumentLoad {
public:
	/// @brief Ask for a load, and run one pass.
	/// @return false when the request is refused: a creation is queued, and is never replaced.
	bool request(const InstrumentLoadRequest& request);

	/// @brief Abandon the browser's load in flight and any browser load queued behind it, telling no one. An
	///        acquisition is not the browser's, and is left alone.
	void cancel();

	/// @brief Advance the load as far as it can go without waiting. Called by the "instrument load" repeating task.
	void routine();

	/// @brief Whether a load is in flight or queued.
	[[nodiscard]] bool inFlight() const { return phase_ != Phase::Idle; }

	/// @brief Whether a commit or clone is in flight: the browser refuses its input until it ends.
	[[nodiscard]] bool commitInFlight() const { return phase_ != Phase::Idle && active_.commit; }

	/// @brief Whether an acquisition is in flight or queued.
	[[nodiscard]] bool acquisitionInFlight() const;

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
		Decide,  ///< Browse: the browser's card-free prologue.
		Walk,    ///< Acquire: the gesture's prologue, then its walk on the storage owner.
		Stage,   ///< Staging the preset document, a tick at a time.
		Parse,   ///< Reading the staged document into a new Instrument or drum.
		Samples, ///< Ticking the new Instrument's (or drum's) sample load.
		Swap,    ///< Browse: the browser swaps it in, in one pass.
		MidiDef, ///< MidiDefinition: staging and reading the definition browser's file into its Instrument.
	};

	bool step();
	bool stepDecide();
	bool stepWalk();
	bool stepStage();
	bool stepParse();
	bool stepSamples();
	bool stepSwap();
	bool stepMidiDef();

	/// @brief Start @p request's load from its prologue.
	void start(const InstrumentLoadRequest& request);
	/// @brief Cancel the load in flight for the request queued behind it, and start that.
	/// @return true: step again.
	bool supersede();
	/// @brief Undo what the load in flight holds that the song does not: its staging, its sample load, a walk's
	///        result still to come, and an Instrument or drum parsed but neither swapped in nor handed over.
	void abandon();
	/// @brief End the load with @p error, tell its completion (or hand an acquisition's Instrument to its gesture's
	///        follow-up, unless @p deliver is false), and start what was queued behind it.
	/// @return Whether a queued load was started, and so whether to step again.
	bool finish(Error error, bool deliver = true);
	/// @brief Stage @p path as a @p firstTagName document; restores `activeDeserializer` on a failure.
	StorageManager::StagedOpen stage(const char* path, const char* firstTagName, const char* altTagName);
	/// @brief Free the staging, and put back `activeDeserializer` as it was before the staging opened.
	void releaseStaging();
	/// @brief The output type the load in flight parses.
	[[nodiscard]] OutputType outputTypeToParse() const;
	/// @brief Begin one sample load over @p instrument, counting it.
	void beginSampleLoad(Instrument& instrument);
	/// @brief Show the name a navigate's walk landed on, as the walk used to before loading it.
	void echoNavigation() const;

	/// @brief The walk, on the storage owner: today's walker for the walk's kind, copied out and the browser's list
	///        emptied.
	///
	/// @note No longer serves AcquireWalk::Unlaunched -- that one runs here, through @ref PresetWalk. The other
	///       three kinds still walk the card start-to-finish and so still belong on the owner.
	static void runWalkOp(void* self);

	/// @brief Arm the Unlaunched search, on the interaction tier.
	void beginWalkHere();
	/// @brief Copy out what the Unlaunched search found, in the shape @ref runWalkOp leaves behind.
	void finishWalkHere();

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
	std::optional<deluge::storage::AsyncSampleLoad> load_{};
	deluge::storage::AcquireResult acquired_{}; ///< Acquire: what is handed over.
	/// @}

	/// @name The walk.
	/// @{
	deluge::storage::AcquireWalkSpec walk_{}; ///< The walk the load in flight asks for.
	bool walkBegun_{false};                   ///< The gesture's prologue has run for the load in flight.
	bool walkDispatched_{false};              ///< The load in flight's walk is queued or has run.
	/// Set as a walk is queued and cleared by the walk as it ends: a walk still out, perhaps an abandoned load's, is
	/// waited for before another is queued, so the two never share @ref walkOut_ and @ref walked_.
	bool walkRunning_{false};
	/// Whether the walk out is running HERE, through @ref walk_machine_, rather than on the owner. Only the
	/// Unlaunched search is; the other three kinds cannot yet be split across a tick.
	bool walkHere_{false};
	/// The Unlaunched search, a folder at a time. Held by value: one walk is out at a time, which is what
	/// @ref walkRunning_ already guarantees.
	PresetWalk walk_machine_{};
	deluge::storage::AcquireWalkSpec walkOut_{}; ///< What the walk out reads; written only while no walk is out.
	AcquireSnapshot walked_{};                   ///< What it found; read only once it has ended.
	/// Where the last navigate's walk landed, while a navigate may still supersede it.
	bool originValid_{false};
	std::string originName_{};
	std::string originDir_{};
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
