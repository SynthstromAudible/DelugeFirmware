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

#include "cluster.h"
#include "util/containers.h"
#include <algorithm>
#include <cstdint>
#include <limits>
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
class ClusterPriorityQueue : public std::priority_queue<qcluster, deluge::fast_vector<qcluster>, Compare> {

public:
	Error enqueueCluster(Cluster& cluster, uint32_t priorityRating) {
		this->push(std::pair(priorityRating, &cluster));
		return Error::NONE;
	}
	[[nodiscard]] constexpr Cluster* front() const { return this->top().second; }
	[[nodiscard]] Cluster* getNext() {
		Cluster* cluster = nullptr;
		while (!this->empty()) {
			cluster = this->front();
			this->pop();
			if (cluster != nullptr) {
				// if this cluster is still wanted then we'll return it
				if ((!cluster->unloadable) && cluster->numReasonsToBeLoaded > 0) {
					return cluster;
				}
				// otherwise get rid of it and continue to the next one
				cluster->destroy();
			}
		}
		return nullptr;
	}

	/// @brief Whether any Cluster is queued at exactly @p priority. Has to check every entry, as the heap only keeps
	/// the front in order.
	[[nodiscard]] bool hasAnyWithPriority(Priority priority) const {
		return std::ranges::any_of(
		    this->c, [priority](const qcluster& entry) { return entry.second != nullptr && entry.first == priority; });
	}

	/// @brief If @p cluster is queued at a lower priority than @p priorityRating, moves it up to that.
	///
	/// Enqueuing it again instead would get it loaded twice, which corrupts any data that gets converted on load.
	/// @return Whether it was queued at a lower priority.
	bool raisePriority(const Cluster* cluster, Priority priorityRating) {
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

	bool erase(const Cluster* cluster) {
		for (auto& val : this->c | std::views::values) {
			if (val == cluster) {
				val = nullptr;
				return true;
			}
		}
		return false;
	};
};
