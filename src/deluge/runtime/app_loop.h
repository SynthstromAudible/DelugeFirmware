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
/// How soon a TIMERLESS job (gap == kNever) is retried after a run that threw, in microseconds.
///
/// A gapped job is retried by its gap regardless of whether a run throws. A timerless job runs only
/// when requested or woken, and a throw unwinds out of the body before it can make either of those
/// happen for itself -- so without this, a single throw would strand it (and whatever it owns: a
/// queue, a held resource) with no way to ever run again. 10 ms is short enough that the retry is not
/// itself a user-visible delay, and long enough that a job thrashing on a repeat failure does not spin
/// the loop.
inline constexpr Micros kThrowRetryUs = 10'000;
/// A set of `DelugeWake` reasons (libdeluge/app.h).
using WakeMask = uint32_t;
using JobId = uint8_t;
inline constexpr JobId kNoJob = 0xFF;

/// A job's run times, from the loop's clock.
struct JobStats {
	const char* name = nullptr;
	Micros maxRun = 0;     ///< the longest run since the last Loop::resetStats(), in µs
	Micros lastRun = 0;    ///< the most recent run, in µs
	uint32_t runs = 0;     ///< runs since the last Loop::resetStats()
	uint32_t failures = 0; ///< runs that threw, since the last Loop::resetStats()
};

/// The interaction tier's jobs, which the BSP's app loop runs through `deluge_app_service`.
///
/// A job runs in a pass when a reason it listens for arrived, when something requested it, or when
/// its gap has passed since its last run ended. A pass runs each such job once, in registration
/// order, so registration order is priority order. Jobs run to completion, one at a time.
///
/// Each service() call runs at most one job, and a pass spans as many calls as it has jobs to run.
/// The caller yields between calls, so the rest of its executor runs between jobs: the storage
/// channels on a single-executor host, the BSP's own tasks on the device. So a job that waits on a
/// channel is answered before the next job runs.
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

	/// Runs @p id at @p whenUs or later: the job's next due time becomes the earlier of its current
	/// one and @p whenUs. Interaction tier only (it writes nextDue, which service() reads). A job
	/// may call this on its own id from inside its own body, to ask for its own next run (a UI
	/// timer deadline, a back-off, a watchdog): service() clears the due time that triggered the
	/// run before the body runs, so a self-request made during the run is kept, not overwritten.
	void requestAt(JobId id, Micros whenUs);

	/// Stops @p id for good. Safe from the job's own body.
	void cancel(JobId id);

	/// Runs the next job of the current pass, starting a pass if none is in progress. A pass runs
	/// every job that is due, woken or requested, once each, in order, one job per call. A job that
	/// throws still counts as having run: the throw is caught, the job's stats advance as for a
	/// normal run, and the pass carries on with the next job, so one misbehaving job cannot starve
	/// the rest. A gapped job is then retried by its gap as usual; a timerless job is retried after
	/// @ref kThrowRetryUs instead, since nothing else would ever run it again.
	///
	/// Reasons that arrive mid-pass reach the jobs still ahead in this pass, and every job in the
	/// next pass.
	/// @return the clock after the run when a job ran (call again after a yield); when the pass has
	///         ended, when the loop next needs a pass (kNever if only a wake or request can make one
	///         due)
	Micros service(Micros now, WakeMask reasons);

	/// Whether a pass is part-way through: call service again after a yield.
	bool inPass() const { return inPass_; }

	/// How many jobs have been added.
	size_t size() const { return count_; }

	/// @p id's run times, or an empty JobStats for an id never added. Read it from the loop's own tier.
	JobStats stats(JobId id) const;

	/// Zeroes every job's worst run, run count and failure count.
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
		uint32_t failures = 0;
	};

	Clock clock_;
	Waker waker_;
	std::array<Job, kMaxJobs> jobs_{};
	size_t count_ = 0;

	bool inPass_ = false;
	/// The job the current pass looks at next.
	size_t cursor_ = 0;
	/// The reasons the current pass runs jobs for.
	WakeMask passReasons_ = 0;
	/// The requests taken when the current pass started.
	std::array<bool, kMaxJobs> passRequested_{};
	/// Reasons that arrived mid-pass, for the next pass. Only reasons some job listens for.
	WakeMask carryReasons_ = 0;
	/// Every reason some added job listens for.
	WakeMask listened_ = 0;
};

/// The app's loop, which deluge_app_service runs.
extern Loop appLoop;

/// The loop's clock, in µs since boot: what jobs and stall bounds measure time with.
Micros nowUs();

} // namespace deluge::runtime
