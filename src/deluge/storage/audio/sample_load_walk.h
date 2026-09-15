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

#include <cstdint>
#include <string_view>

class AudioClip;
class AudioFileHolder;

namespace deluge::storage {

/// @brief Whether a demand walk goes on after a checkpoint.
enum class Walk : uint8_t {
	Continue, ///< Visit the rest of the tree.
	Stop,     ///< The load was aborted: return without visiting anything more at this level or below it.
};

/// @brief When an instrument's own folder becomes the load's context for its samples.
enum class ScopeRule : uint8_t {
	WhenReading, ///< Only when the load may read the card, and no load context is in effect yet.
	Always,      ///< Whenever no load context is in effect yet.
};

/// @brief One audio file the project needs: the holder that names it, and how to bind what is found.
struct SampleDemand {
	/// The holder whose `filePath` names the file and whose `audioFileType` says what kind of file to find.
	AudioFileHolder* holder = nullptr;
	/// Bind the file reversed.
	bool reversed = false;
	/// Bind the file as one the user picked: the zone is reset to the whole file, the loop points come from the file,
	/// and the sample browser records the path as the last one loaded.
	bool manuallySelected = false;
	/// How the bound file's clusters are claimed.
	int32_t clusterLoadInstruction = CLUSTER_ENQUEUE;
	/// Interpret the file as a wavetable even without the tag or length that normally identify one.
	bool makeWaveTableWorkAtAllCosts = false;
	/// The after-bind step. Non-null for an audio clip's sample: the clip takes the holder's path as its name, and a
	/// failed lookup is shown.
	AudioClip* clipToName = nullptr;
};

/// @brief What a demand walk reports to.
///
/// The model tree walks its holders and calls these in load order; it loads nothing itself. A driver decides what
/// each call does: BlockingSampleLoad loads each demand as it arrives, exactly as the tree's own loads used to.
class SampleLoadVisitor {
public:
	virtual ~SampleLoadVisitor() = default;

	/// @brief Before a source's range @p rangeIndex is demanded.
	/// @return Walk::Stop to abandon the source, and the walk of everything above it.
	virtual Walk rangeCheckpoint(int32_t rangeIndex) = 0;

	/// @brief Before a kit's drum is walked.
	/// @return Walk::Stop to abandon the kit.
	virtual Walk drumCheckpoint() = 0;

	/// @brief Before each of a song's clips is looked at, whether or not it demands anything.
	virtual void clipCheckpoint() = 0;

	/// @brief A kit or synth is about to be walked. Its samples may also be in its own folder, "<dirPath>/<name>".
	/// @param thing ThingType::KIT or ThingType::SYNTH.
	/// @param rule  When that folder applies: see ScopeRule.
	virtual void beginInstrumentScope(std::string_view dirPath, std::string_view name, ThingType thing,
	                                  ScopeRule rule) = 0;

	/// @brief The walk of the instrument whose beginInstrumentScope() came last has ended, stopped or not.
	virtual void endInstrumentScope() = 0;

	/// @brief One file the project needs.
	virtual void demand(const SampleDemand& demand) = 0;
};

} // namespace deluge::storage
