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
#include "runtime/app_loop.h"

#include <algorithm>
#include <libdeluge/app.h>
#include <libdeluge/scheduler.h>

namespace deluge::runtime {

namespace {
Micros bspClock() {
	return static_cast<Micros>(getSystemTime() * 1'000'000.0);
}
void wakeBsp() {
	deluge_app_wake(DELUGE_WAKE_APP);
}
} // namespace

Loop appLoop{bspClock, wakeBsp};

JobId Loop::add(const char* name, void (*body)(), Micros gap, WakeMask wakes) {
	if (count_ == kMaxJobs) {
		return kNoJob;
	}
	Job& job = jobs_[count_];
	job.name = name;
	job.body = body;
	job.gap = gap;
	job.wakes = wakes;
	listened_ |= wakes;
	job.nextDue = gap == kNever ? kNever : clock_() + gap;
	return static_cast<JobId>(count_++);
}

void Loop::request(JobId id) {
	if (id >= count_) {
		return;
	}
	jobs_[id].requested.store(true, std::memory_order_release);
	waker_();
}

void Loop::cancel(JobId id) {
	if (id < count_) {
		jobs_[id].cancelled = true;
	}
}

Micros Loop::service(Micros now, WakeMask reasons) {
	if (!inPass_) {
		// Requests made during this pass, a job's own included, belong to the next one: take this
		// pass's requests before running anything.
		for (size_t i = 0; i < count_; ++i) {
			passRequested_[i] = jobs_[i].requested.exchange(false, std::memory_order_acq_rel);
		}
		passReasons_ = reasons | carryReasons_;
		carryReasons_ = 0;
		cursor_ = 0;
		inPass_ = true;
	}
	else {
		// Jobs still ahead in this pass see the new reasons now; those already passed see them next pass.
		passReasons_ |= reasons;
		carryReasons_ |= reasons & listened_;
	}

	for (size_t i = cursor_; i < count_; ++i) {
		Job& job = jobs_[i];
		if (job.cancelled) {
			continue;
		}
		const bool woken = (passReasons_ & job.wakes) != 0;
		if (!woken && !passRequested_[i] && now < job.nextDue) {
			continue;
		}
		const Micros start = clock_();
		job.body();
		now = clock_();
		const Micros ran = now - start;
		job.lastRun = ran;
		job.maxRun = std::max(job.maxRun, ran);
		++job.runs;
		if (job.gap != kNever) {
			job.nextDue = now + job.gap;
		}
		cursor_ = i + 1;
		return now;
	}

	inPass_ = false;
	if (carryReasons_ != 0) {
		return now;
	}
	Micros next = kNever;
	for (size_t i = 0; i < count_; ++i) {
		const Job& job = jobs_[i];
		if (job.cancelled) {
			continue;
		}
		if (job.requested.load(std::memory_order_acquire)) {
			return now;
		}
		next = std::min(next, job.nextDue);
	}
	return next;
}

JobStats Loop::stats(JobId id) const {
	if (id >= count_) {
		return {};
	}
	const Job& job = jobs_[id];
	return {job.name, job.maxRun, job.lastRun, job.runs};
}

void Loop::resetStats() {
	for (size_t i = 0; i < count_; ++i) {
		jobs_[i].maxRun = 0;
		jobs_[i].runs = 0;
	}
}

} // namespace deluge::runtime
