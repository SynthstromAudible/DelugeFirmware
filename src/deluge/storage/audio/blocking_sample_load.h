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
#include "storage/audio/audio_load_context.h"
#include "storage/audio/sample_load_walk.h"

#include <cstdint>
#include <string_view>

namespace deluge::storage {

/// @brief The blocking driver: loads each demand of a walk as it arrives, on the calling tier, in walk order.
///
/// It makes a walk do exactly what the model tree's own loads did, call for call:
/// - a source range: the audio routine on every fourth range, then the abort check when reading;
/// - a kit's drum: the abort check when reading;
/// - a song's clip: the audio routine on every eighth clip, only when not reading;
/// - a kit or synth: its own folder becomes the load's context when no context is in effect (see ScopeRule);
/// - a demand: the lookup (AudioFileHolder::acquire), the bind (AudioFileHolder::setAudioFile), then the after-bind
///   step. A failed lookup stops nothing; only an abort stops a walk.
class BlockingSampleLoad final : public SampleLoadVisitor {
public:
	/// @param mayReadCard false confines every lookup to files already in memory.
	/// @param context     The load the walk belongs to, or null for none.
	BlockingSampleLoad(bool mayReadCard, AudioLoadContext* context);

	Walk rangeCheckpoint(int32_t rangeIndex) override;
	Walk drumCheckpoint() override;
	void clipCheckpoint() override;
	void beginInstrumentScope(std::string_view dirPath, std::string_view name, ThingType thing,
	                          ScopeRule rule) override;
	void endInstrumentScope() override;
	void demand(const SampleDemand& demand) override;

	/// @brief What a walk's result means to a caller that returns an Error: only an abort stops a walk.
	[[nodiscard]] static Error errorFor(Walk walk) {
		return (walk == Walk::Stop) ? Error::ABORTED_BY_USER : Error::NONE;
	}

private:
	/// @brief Walk::Stop when the load may read the card and the user has asked to abort it.
	[[nodiscard]] Walk stopIfAborted() const;

	bool mayReadCard_;
	/// The context each lookup runs under: the caller's, or ownFolder_ while an instrument's scope holds it.
	AudioLoadContext* context_;
	/// The one instrument folder a walk can have open. While it is open a context is in effect, so no scope inside
	/// it opens another.
	AudioLoadContext ownFolder_{};
	/// context_ as it was when ownFolder_ was opened.
	AudioLoadContext* contextOutsideOwnFolder_{nullptr};
	/// How many instrument scopes are open.
	int32_t scopeDepth_{0};
	/// The scope depth ownFolder_ was opened at, or -1 when it is not open.
	int32_t ownFolderDepth_{-1};
	/// Clips seen while not reading the card: the audio routine runs on every eighth, starting with the first.
	uint32_t clipsWithoutReading_{0};
};

} // namespace deluge::storage
