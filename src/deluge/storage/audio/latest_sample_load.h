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
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_load_context.h"
#include "storage/audio/sample_load_walk.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace deluge::storage {

/// @brief One holder's sample load, latest-wins, ticked by its consumer.
///
/// Holds at most one AsyncSampleLoad, of a single holder, and a payload the consumer gets back with that load's
/// outcome. A new request() replaces the held load, and cancel() drops it. A dropped load reports nothing; its request
/// stays with the loader, where a later lookup of the same path collects it.
///
/// Latest-wins here is replacement, not single flight: a tick dispatches nothing, so there is no flight to keep.
/// The consumer sets the holder's path, and unbinds it, before request(); this touches the holder only through the
/// load.
///
/// Runs on the calling tier. Not thread-safe: the same contract as LatestWins.
///
/// @tparam T What the consumer needs back when the load ends (for the sample browser: the path and the scroll
///           direction).
template <typename T>
class LatestSampleLoad {
public:
	/// @brief How a held load ended.
	enum class Result : uint8_t {
		Bound,  ///< The holder has its file.
		Empty,  ///< The holder settled with no file: Outcome::error says why.
		Failed, ///< The load ended early: Outcome::reason says why.
	};

	/// @brief What tick() reports when the held load ends.
	struct Outcome {
		Result result;
		AsyncSampleLoad::Reason reason; ///< AsyncSampleLoad::Reason::None unless @ref result is Failed.
		Error error;                    ///< The lookup's error for an Empty holder; Error::NONE otherwise.
		T payload;                      ///< What request() was given with this load.
	};

	/// @brief Replace any held load with a load of @p demand's holder.
	/// @param demand  The holder, and how to bind what is found.
	/// @param context The load context, or null for a file that belongs to no song or preset load.
	/// @param payload Handed back with this load's outcome.
	void request(const SampleDemand& demand, AudioLoadContext* context, T payload) {
		load_.emplace(AsyncSampleLoad::forHolder(demand, context));
		holder_ = demand.holder;
		payload_ = std::move(payload);
	}

	/// @brief Drop the held load. Nothing is reported for it.
	void cancel() { load_.reset(); }

	/// @brief Whether a load is held.
	[[nodiscard]] bool pending() const { return load_.has_value(); }

	/// @brief Tick the held load once.
	/// @return nullopt when no load is held or it is still loading; otherwise its outcome, and the load is released.
	std::optional<Outcome> tick() {
		if (!load_.has_value()) {
			return std::nullopt;
		}
		const AsyncSampleLoad::Progress progress = load_->tick();
		if (progress.state == AsyncSampleLoad::State::Loading) {
			return std::nullopt;
		}
		Result result = Result::Failed;
		if (progress.state == AsyncSampleLoad::State::Loaded) {
			result = (holder_ != nullptr && holder_->audioFile != nullptr) ? Result::Bound : Result::Empty;
		}
		load_.reset();
		return Outcome{result, progress.reason, progress.error, std::move(payload_)};
	}

private:
	std::optional<AsyncSampleLoad> load_{};
	/// The held load's holder, read once the load ends to tell Bound from Empty.
	AudioFileHolder* holder_{nullptr};
	T payload_{};
};

} // namespace deluge::storage
