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

#include <optional>

namespace deluge::storage {

/// @brief Single-flight dispatch with latest-wins supersession.
///
/// A high-frequency producer (e.g. sample preview on every cursor move) records a
/// desired target; at most one dispatch runs at a time, and when it completes the
/// *latest* requested target (if it changed meanwhile) is dispatched next — so the
/// result converges on where the user actually landed, never a stale intermediate.
/// Contrast `Coalescer`, which drops new requests while one is in flight (correct for
/// demand-collapsing, wrong for "show the newest").
///
/// Not thread-safe: all calls are expected from the single executor-thread context
/// that also runs the dispatched op (same contract as `Coalescer`).
template <typename T>
class LatestWins {
public:
	/// @brief Record a desired target.
	///
	/// @param target The desired target.
	/// @return true if the caller should dispatch now (nothing was in flight); false if an op is
	///         already running — it will pick up this target on completion.
	bool request(const T& target) {
		if (in_flight_) {
			queued_ = target;
			return false;
		}
		in_flight_ = true;
		current_ = target;
		queued_.reset();
		return true;
	}

	/// @brief Signal that the in-flight op finished.
	///
	/// @return The next target to dispatch if the desired target changed while running
	///         (latest-wins re-dispatch), or nullopt if nothing is pending (goes idle).
	std::optional<T> complete() {
		if (queued_.has_value()) {
			current_ = *queued_;
			queued_.reset();
			return current_; // stays in_flight_ — caller re-dispatches current_
		}
		in_flight_ = false;
		return std::nullopt;
	}

	/// @brief Force back to idle, discarding any queued target.
	///
	/// For when a dispatch the caller believed it had issued never actually ran (e.g. the owner
	/// queue was full, so the op that would have called complete() will never fire) — without this
	/// the single-flight guard would wedge forever. The dropped target is simply lost; the next
	/// request() supersedes it.
	void reset() {
		in_flight_ = false;
		queued_.reset();
	}

	/// @brief Discard the queued target, KEEPING the single-flight guard.
	///
	/// The half of reset() that applies when the dispatched op is still genuinely out there and will
	/// still report back, but what was queued behind it is no longer wanted. Releasing the guard as
	/// well would let a second op dispatch alongside the running one. Nothing is queued unless an op
	/// is in flight, so on an idle instance this does nothing.
	void clear_queued() { queued_.reset(); }

	[[nodiscard]] bool in_flight() const { return in_flight_; }
	[[nodiscard]] const T& current() const { return current_; }

	/// @brief The target a further request() would OVERWRITE, if any.
	///
	/// Latest-wins supersession is only correct while every target names the same kind of thing (a
	/// destination to converge on). A caller whose targets are not all of that kind can look here
	/// first and decline the new request instead, keeping what is queued -- which is what
	/// `LoadInstrumentPresetUI::dispatchPresetAcquisition()` does for creation gestures.
	/// @return The queued target, or nullopt when nothing is queued (always so while idle).
	[[nodiscard]] const std::optional<T>& queued() const { return queued_; }

private:
	bool in_flight_ = false;
	T current_{};
	std::optional<T> queued_{};
};

} // namespace deluge::storage
