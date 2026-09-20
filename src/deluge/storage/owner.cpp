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

#include "storage/owner.h"

#include "libdeluge/worker.h"

namespace deluge::storage {

namespace {
/// Ops handed to the owner since boot. Not atomic: it is incremented only by dispatching contexts, which are
/// thread-mode, and read for evidence rather than for control.
uint32_t g_opsRun = 0;
} // namespace

bool Owner::run(void (*fn)(void*), void* ctx) {
	const bool dispatched = deluge_worker_run(fn, ctx);
	g_opsRun += dispatched ? 1 : 0;
	return dispatched;
}

uint32_t Owner::opsRun() {
	return g_opsRun;
}

bool Owner::run_sd_routine(void (*fn)(void*), void* ctx) {
	const bool dispatched = deluge_worker_run_sd_routine(fn, ctx);
	g_opsRun += dispatched ? 1 : 0;
	return dispatched;
}

namespace {
/// How many times on_owner() has answered TRUE after the owner started. See Owner::onOwnerTrue().
int32_t g_onOwnerTrue = 0;
} // namespace

bool Owner::on_owner() {
	const bool onOwner = deluge_worker_on_worker();
	if (onOwner && deluge_worker_started()) {
		g_onOwnerTrue++;
	}
	return onOwner;
}

int32_t Owner::onOwnerTrue() {
	return g_onOwnerTrue;
}

bool Owner::started() {
	return deluge_worker_started();
}

bool Owner::run_or_inline(void (*fn)(void*), void* ctx) {
	if (on_owner()) {
		fn(ctx);
		return true;
	}
	return run(fn, ctx);
}

void Coalescer::request(void (*fill)(void*), void* ctx) {
	if (in_flight_.exchange(true, std::memory_order_acq_rel)) {
		return; // a dispatch is already in flight — it covers this demand
	}
	fill_ = fill;
	ctx_ = ctx;
	bool dispatched;
	if (sd_routine_) {
		dispatched = Owner::run_sd_routine(&Coalescer::run_and_release, this);
	}
	else {
		dispatched = Owner::run(&Coalescer::run_and_release, this);
	}
	if (!dispatched) {
		// Dispatch was dropped (owner queue full) → run_and_release will never fire, so
		// release the guard here or the Coalescer would wedge single-flight forever.
		in_flight_.store(false, std::memory_order_release);
	}
}

void Coalescer::run_and_release(void* self_) {
	auto* self = static_cast<Coalescer*>(self_);
	self->fill_(self->ctx_);
	self->in_flight_.store(false, std::memory_order_release);
}

} // namespace deluge::storage
