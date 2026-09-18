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
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/latest_sample_load.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deluge::gui {

/// @brief One undo or redo, on the interaction tier.
///
/// A reversion reads the card for exactly one reason: `ConsequenceAudioClipSetSample::revert()` binds the file the
/// clip is going back to, and binding reads it if it is not already in memory. That one leaf is why the whole
/// reversion used to be dispatched onto the storage owner.
///
/// So the read happens first, here, and the reversion itself runs card-free:
/// - **Collect** asks the action logger which action the command would revert and copies out the file every
///   consequence in it will want (@ref deluge::storage::Consequence::filePathNeededForRevert). An action with none --
///   a note edit, a clip move, a swing change -- skips straight to Revert in the same pass.
/// - **Samples** loads those files, one at a time, through the loader every other consumer uses, and holds a
///   reference to each so the reversion finds them in memory. A file that cannot be loaded is reported once and does
///   not stop the reversion: `revert()` shows the error and carries on with an empty holder, which is what it has
///   always done.
/// - **Revert** runs `ActionLogger::undo()` or `::redo()` in one pass. Nothing in it reaches the card, and the model
///   moves exactly as it did when the whole thing ran on the owner.
///
/// A command arriving while one is in flight is REFUSED, not queued: undo and redo are steps rather than
/// destinations, and silently dropping one would lose an edit.
class Reversion {
public:
	/// @brief Which way to move through the log.
	enum class Direction : uint8_t { Undo, Redo };

	/// @brief Ask for one reversion, and run one pass.
	/// @return false when the request is refused because one is already in flight.
	bool request(Direction direction);

	/// @brief Whether a reversion is in flight.
	[[nodiscard]] bool inFlight() const { return phase_ != Phase::Idle; }

	/// @brief Advance the reversion as far as it can go without waiting. Called by the "reversion" repeating task.
	void routine();

	/// @name Counts for the clear-song-undo gate.
	/// @{
	/// Reversions that have run.
	[[nodiscard]] int32_t reversions() const { return reversions_; }
	/// Files pre-loaded for them.
	[[nodiscard]] int32_t preloads() const { return preloads_; }
	/// Commands refused because one was already in flight.
	[[nodiscard]] int32_t refused() const { return refused_; }
	/// @}

private:
	enum class Phase : uint8_t {
		Idle,    ///< No reversion.
		Collect, ///< Reading the action's consequences for the files they will want.
		Samples, ///< Loading those files, one at a time.
		Revert,  ///< Running the reversion itself, card-free.
	};

	bool step();
	bool stepCollect();
	bool stepSamples();
	bool stepRevert();
	/// @brief Drop the references the pre-load took, once the reversion has used them.
	void releaseHeld();

	Phase phase_{Phase::Idle};
	Direction direction_{Direction::Undo};
	/// The files the reversion's consequences will want, copied out of the log: plain values, because the list they
	/// came from belongs to the model and is rewritten as the reversion runs.
	std::vector<std::string> wanted_{};
	size_t nextWanted_{0};
	/// The holder a pre-load binds, which is what keeps the file in memory until the reversion has taken it.
	AudioFileHolder held_{};
	deluge::storage::LatestSampleLoad<int32_t> load_{};

	int32_t reversions_{0};
	int32_t preloads_{0};
	int32_t refused_{0};
};

extern Reversion reversion;

} // namespace deluge::gui
