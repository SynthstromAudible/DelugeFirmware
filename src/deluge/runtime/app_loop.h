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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace deluge::runtime {

/// Microseconds on the BSP's monotonic clock.
using Micros = uint64_t;
/// A time that never comes: a job with no timer, or a loop with nothing due.
inline constexpr Micros kNever = ~Micros{0};
/// A set of `DelugeWake` reasons (libdeluge/app.h).
using WakeMask = uint32_t;
using JobId = uint8_t;
inline constexpr JobId kNoJob = 0xFF;

/// A job's run times, from the loop's clock.
struct JobStats {
	const char* name = nullptr;
	Micros maxRun = 0;  ///< the longest run since the last Loop::resetStats(), in µs
	Micros lastRun = 0; ///< the most recent run, in µs
	uint32_t runs = 0;  ///< runs since the last Loop::resetStats()
};

/// The interaction tier's jobs, which the BSP's app loop runs through `deluge_app_service`.
///
/// A job runs in a pass when a reason it listens for arrived, when something requested it, or when
/// its gap has passed since its last run ended. A pass runs each such job once, in registration
/// order, so registration order is priority order. Jobs run to completion, one at a time.
class Loop {
public:
	using Clock = Micros (*)();
	using Waker = void (*)();
	static constexpr size_t kMaxJobs = 32;

	/// @param clock the BSP's clock, read around each run
	/// @param waker asks the BSP for a pass; called by request()
	Loop(Clock clock, Waker waker) : clock_{clock}, waker_{waker} {}

	/// Adds a job.
	/// @param gap the time from the end of a run to the next run, or kNever for no timer
	/// @param wakes the reasons that run the job
	/// @return the job's id, or kNoJob if the list is full
	JobId add(const char* name, void (*body)(), Micros gap, WakeMask wakes);

	/// Runs @p id in the next pass. Safe from any tier.
	void request(JobId id);

	/// Stops @p id for good. Safe from the job's own body.
	void cancel(JobId id);

	/// Runs one pass: every job that is due, woken or requested, once each, in order.
	/// @return when the loop next needs a pass (kNever if only a wake or request can make one due)
	Micros service(Micros now, WakeMask reasons);

	/// How many jobs have been added.
	size_t size() const { return count_; }

	/// @p id's run times, or an empty JobStats for an id never added. Read it from the loop's own tier.
	JobStats stats(JobId id) const;

	/// Zeroes every job's worst run and run count.
	void resetStats();

private:
	struct Job {
		const char* name = nullptr;
		void (*body)() = nullptr;
		Micros gap = kNever;
		WakeMask wakes = 0;
		Micros nextDue = kNever;
		bool cancelled = false;
		std::atomic<bool> requested{false};
		Micros maxRun = 0;
		Micros lastRun = 0;
		uint32_t runs = 0;
	};

	Clock clock_;
	Waker waker_;
	std::array<Job, kMaxJobs> jobs_{};
	size_t count_ = 0;
};

} // namespace deluge::runtime
