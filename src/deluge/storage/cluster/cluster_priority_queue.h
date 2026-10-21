/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"
#include "cluster.h"
#include "util/containers.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>

using Priority = uint32_t;
using qcluster = std::pair<Priority, Cluster*>;

/// Priorities for loading Clusters. Lower numbers load first. From first to last:
/// - AudioClips: forAudioClip(), which puts each one ahead of every Voice of the same VoicePriority.
/// - Voices: Voice::getPriorityRating(), which stays at or below 0xFEFFFFFF.
/// - The first Clusters of each Sample, so it can start instantly. LoadSongUI waits for these.
/// - Speculative loads that nothing's waiting on yet, like prefetches and waveform drawing.
/// - Retrying a load that failed, presumably because the card's gone.
namespace ClusterPriority {
constexpr Priority kSampleStart = 0xFFFFFFFD;
constexpr Priority kSpeculative = 0xFFFFFFFE;
constexpr Priority kRetryAfterCardError = 0xFFFFFFFF;

/// Voice::getPriorityRating() puts the VoicePriority in the top two bits, with at least one active Voice counted below
/// them, so leaving the rest zero puts the AudioClip ahead of all Voices of that VoicePriority.
constexpr Priority forAudioClip(VoicePriority voicePriority) {
	return static_cast<Priority>(3 - util::to_underlying(voicePriority)) << 30;
}
} // namespace ClusterPriority

// comparator returns true if elements need to be swapped
class Compare {
public:
	bool operator()(const qcluster& first, const qcluster& second) const { return first.first > second.first; }
};

/// Clusters waiting to be read from the card. The main thread adds them, and the SD interrupt takes them off the front
/// with popForLoading() as it gets to them - so everything here has to be done with interrupts disabled.
class ClusterPriorityQueue : private std::priority_queue<qcluster, deluge::fast_vector<qcluster>, Compare> {

public:
	Error enqueueCluster(Cluster& cluster, uint32_t priorityRating, const InterruptsDisabled&) {
		this->push(std::pair(priorityRating, &cluster));
		return Error::NONE;
	}

	/// Not exact unless interrupts are disabled, as the SD interrupt may be taking one off
	[[nodiscard]] bool empty() const { return this->c.empty(); }

	/// @brief Takes the highest priority Cluster that still wants loading off the queue. For the SD interrupt, so
	/// doesn't allocate or free anything - unwanted ones are just dropped, as they've already been taken care of by
	/// whatever made them unwanted.
	/// @return It and its priority, or nullopt if there's nothing to load.
	std::optional<qcluster> popForLoading(const InterruptsDisabled&) {
		while (!this->c.empty()) {
			qcluster entry = this->top();
			this->pop();
			Cluster* cluster = entry.second;
			if (cluster != nullptr && !cluster->unloadable && cluster->numReasonsToBeLoaded > 0) {
				return entry;
			}
		}
		return std::nullopt;
	}

	/// @brief Whether any Cluster is queued at exactly @p priority. Has to check every entry, as the heap only keeps
	/// the front in order.
	[[nodiscard]] bool hasAnyWithPriority(Priority priority, const InterruptsDisabled&) const {
		return std::ranges::any_of(
		    this->c, [priority](const qcluster& entry) { return entry.second != nullptr && entry.first == priority; });
	}

	/// @brief If @p cluster is queued at a lower priority than @p priorityRating, moves it up to that.
	///
	/// Enqueuing it again instead would get it loaded twice, which corrupts any data that gets converted on load.
	/// @return Whether it was queued at a lower priority.
	bool raisePriority(const Cluster* cluster, Priority priorityRating, const InterruptsDisabled&) {
		bool raised = false;
		for (auto& [priority, val] : this->c) {
			if (val == cluster && priorityRating < priority) {
				priority = priorityRating;
				raised = true;
			}
		}
		if (raised) {
			std::ranges::make_heap(this->c, this->comp);
		}
		return raised;
	}

	/// @brief Takes @p cluster out of the queue, e.g. to load it right away.
	/// @return The priority it was queued at, so it can be put back if loading fails, or nullopt if it wasn't queued.
	std::optional<Priority> take(const Cluster* cluster, const InterruptsDisabled&) {
		std::optional<Priority> taken;
		for (auto& [priority, val] : this->c) {
			if (val == cluster) {
				if (!taken || priority < *taken) {
					taken = priority;
				}
				val = nullptr;
			}
		}
		return taken;
	}

	bool erase(const Cluster* cluster, const InterruptsDisabled&) {
		for (auto& val : this->c | std::views::values) {
			if (val == cluster) {
				val = nullptr;
				return true;
			}
		}
		return false;
	};
};
