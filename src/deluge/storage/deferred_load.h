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

#include "storage/latest_wins.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace deluge::storage {

/// @brief Latest-wins single-flight dispatch for a load that may answer "not yet", plus the
///        cancellation the deferral makes necessary.
///
/// `LatestWins` alone is enough while every dispatched op produces its result before returning.
/// An asynchronous load does not: it can answer "still loading", and the only way that answer
/// ever becomes a result is if something asks again. That re-ask is what this adds — and with it,
/// two problems `LatestWins` has no way to express:
///
///  1. A target the user has LEFT must not be rendered. Between dispatch and the op actually
///     running, the selection can move somewhere that has no target at all (a folder, an empty
///     list) — a move that dispatches no replacement, so latest-wins supersession never fires.
///     Every attempt therefore carries a GENERATION, and `cancel()` moves it; an op whose attempt
///     is stale must neither act on its result nor re-ask for it.
///  2. A deferral has to be remembered somewhere the caller's per-tick surface can find it. That
///     is `take_pending()`.
///
/// The generation moves on `cancel()` and on nothing else. In particular it does NOT move on
/// `request()`: moving from one target to the next is ordinary supersession, and the in-flight op
/// must be allowed to finish and hand off to the newer target the way it always has.
///
/// Not thread-safe: all calls are expected from the single executor-thread context that also runs
/// the dispatched op (same contract as `LatestWins`).
template <typename T>
class DeferredLoad {
public:
	/// @brief A dispatched attempt: what an op is working on, and whether that is still wanted.
	///
	/// The generation is the one in force when the target BECAME current — not the one in force
	/// when the op got round to reading it. That distinction is the whole mechanism: an op reads its
	/// attempt as it starts running, which on a queued dispatch is already after the cancellation it
	/// has to notice.
	struct Attempt {
		T target;
		uint32_t generation;
	};

	/// @brief Record a desired target, superseding any pending re-ask.
	///
	/// The pending re-ask is dropped because it names a target this one replaces: re-driving it
	/// would land a result for something the caller has moved off. The deferred work itself carries
	/// on wherever it is running, and its result is either picked up by a later request for the same
	/// target or discarded.
	///
	/// @param target The desired target.
	/// @return true if the caller should dispatch now (nothing was in flight); false if an op is
	///         already running — it will pick up this target on completion.
	bool request(const T& target) {
		pending_.reset();
		const bool dispatch = flight_.request(target);
		if (dispatch) {
			current_generation_ = generation_; // this target is current as of now
		}
		// Otherwise it is queued behind a running op, and becomes current in complete() — which is
		// where its generation is stamped, since a cancellation may land in between.
		return dispatch;
	}

	/// @brief The attempt a dispatched op is to work on.
	///
	/// Call this at the top of the op, before doing anything else with it.
	[[nodiscard]] Attempt attempt() const { return Attempt{flight_.current(), current_generation_}; }

	/// @brief Is @p a still wanted?
	///
	/// Check this BEFORE doing the work, not merely before recording a deferral. Rendering a
	/// cancelled target is the damage; a stale re-ask is only the mechanism that keeps rendering it.
	[[nodiscard]] bool is_current(const Attempt& a) const { return a.generation == generation_; }

	/// @brief Report that the op working on @p a has finished.
	///
	/// @param a        The attempt the op was dispatched with.
	/// @param deferred Whether the work answered "not finished yet" and has to be asked for again.
	/// @return The next target to dispatch if a newer one arrived while this op ran (latest-wins
	///         re-dispatch), or nullopt. On nullopt with @p deferred set and @p a still current, the
	///         target is armed for `take_pending()` instead.
	std::optional<T> complete(const Attempt& a, bool deferred) {
		if (auto next = flight_.complete(); next.has_value()) {
			// The newer target supersedes `a` even if `a` deferred: re-driving the older one would
			// land a result for something the caller has already left. If the op about to run defers
			// in turn, it records its OWN deferral.
			pending_.reset();
			current_generation_ = generation_; // it is current as of now, not as of when it was queued
			return next;
		}
		if (deferred && is_current(a)) {
			pending_ = a.target;
		}
		// A stale attempt arriving here is what releases the single-flight guard `cancel()`
		// deliberately left held — see cancel().
		return std::nullopt;
	}

	/// @brief Abandon everything: nothing is wanted any more.
	///
	/// For when the caller moves somewhere with no target at all, so no replacement is dispatched
	/// and supersession cannot fire.
	///
	/// This deliberately does NOT release the single-flight guard. An op genuinely is still out
	/// there, and clearing the guard here would let a second one dispatch alongside it. The guard is
	/// released when that op reports back and `complete()` finds its attempt stale. (Which is why
	/// `LatestWins::reset()` is the wrong thing to reach for: it releases the guard AND leaves the
	/// already-dispatched op free to act on a target the caller has left.)
	void cancel() {
		generation_++;
		pending_.reset();
		// The queued target went in before the cancellation, so it is abandoned too. Dropped without
		// releasing the guard, and without touching `current_generation_`: whatever is still in flight
		// keeps the stamp it already had, which the bump above has just made stale.
		flight_.clear_queued();
	}

	/// @brief Take the target of a load that deferred, for the caller's per-tick re-ask.
	///
	/// Taken OUT rather than peeked at: where the dispatch runs the op inline, the re-dispatch
	/// completes before this returns and may arm a SECOND deferral, which clearing afterwards would
	/// wipe.
	///
	/// @return The target to ask for again, or nullopt if nothing deferred.
	std::optional<T> take_pending() { return std::exchange(pending_, std::nullopt); }

	/// @brief Whether a load deferred and is waiting to be asked for again.
	[[nodiscard]] bool pending() const { return pending_.has_value(); }

	/// @brief Report that a dispatch the caller believed it had issued will never run.
	///
	/// Releases the single-flight guard (which would otherwise wedge forever) and arms the target
	/// for `take_pending()`, so the per-tick surface asks again rather than the target being lost
	/// with nothing sounded, drawn or reported.
	void release_dispatch() {
		// `current()` is the target of the dispatch that failed: `request()` only returns true —
		// which is the only thing that leads here — after making it current.
		pending_ = flight_.current();
		flight_.reset();
	}

	/// @brief Whether an op is dispatched and has not reported back.
	[[nodiscard]] bool in_flight() const { return flight_.in_flight(); }

private:
	LatestWins<T> flight_{};
	std::optional<T> pending_{};
	/// Moves on cancel(), and on nothing else. The value everything is judged against.
	uint32_t generation_ = 0;
	/// The generation in force when `flight_.current()` became current.
	uint32_t current_generation_ = 0;
};

} // namespace deluge::storage
