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

#include <atomic>

namespace deluge::storage {

/// @brief The single storage owner: routes FatFS-touching operations through
/// one serialized context so they never re-enter non-reentrant FatFS.
///
/// On Embassy the op runs on the worker fiber (so its `yield()`s / mid-transfer
/// suspends don't stall the caller); on legacy/host it runs inline on the caller's
/// stack. Fire-and-forget: `run` returns immediately (Embassy) / on completion
/// (inline). Post-op work must live inside the op. `await` (a blocking, result-
/// returning variant) is a later rung; this seam is `run`-only.
///
/// This is the one enforced entry point for the single-owner discipline an
/// async-SD reentrancy stress proof showed is load-bearing. See
/// docs/superpowers/specs/2026-07-17-async-sd-staging-ladder-roadmap.md.
struct Owner {
	/// @brief Queue `fn(ctx)` on the owner. C-ABI-shaped for existing call sites.
	/// @param fn  The op to run.
	/// @param ctx Context pointer passed through to `fn`.
	/// @return true if the op ran (inline on legacy/host) or was queued (Embassy fiber);
	///         false if the dispatch was dropped (Embassy worker queue full) — the op will
	///         NOT run, so a coalescing caller must reset its single-flight guard.
	static bool run(void (*fn)(void*), void* ctx);

	/// @brief Like run(), but the op is SD-routine-class: RESOURCE_SD_ROUTINE tasks are
	/// held off for its whole in-flight window (enqueue → completion) so a
	/// consumer whose op frees an object (the recorder) can't be freed
	/// concurrently by another task. Inline on legacy/host (indistinguishable
	/// from run() there); Embassy takes an SD-routine hold across the op.
	/// @param fn  The op to run.
	/// @param ctx Context pointer passed through to `fn`.
	/// @return as run(): true if it ran/queued, false if the dispatch was dropped.
	static bool run_sd_routine(void (*fn)(void*), void* ctx);

	/// @brief True iff the current context is the storage worker (the worker fiber on
	/// Embassy; always false on cooperative/host, where run() is inline). Use to
	/// avoid re-dispatching a dual-context op that is already on the worker.
	/// @return true iff on the storage worker context.
	static bool on_owner();

	/// @brief True iff the owner has run at least once - i.e. an owner exists to be `on_owner()`
	/// of at all. False only during the narrow pre-owner boot window (the synchronous
	/// `deluge_boot()` call chain, before the worker pump's first poll); true for the rest of the
	/// process's life on Embassy. Always false on cooperative/host, where there is no separate
	/// owner to have started (see `deluge_worker_started()`'s doc).
	///
	/// Exists for the one legal exception to "every reader that reaches the owner must be
	/// `on_owner()`": `Song::ensureAtLeastOneSessionClip()` calls straight into
	/// `LoadInstrumentPresetUI`'s readers before any interaction tier - or owner - exists yet, so
	/// `on_owner()` alone would wrongly indict it. A caller asserts
	/// `on_owner() || !started()` rather than `on_owner()` alone.
	/// @return true iff the storage owner has run at least once.
	static bool started();

	/// @brief Run `fn(ctx)` on the worker, but inline if already on_owner() (or on a
	/// cooperative/host BSP). For dual-context callers: the leaf work runs on the
	/// worker exactly once, whether reached from a UI handler (dispatched) or from
	/// inside another worker op (inline).
	/// @param fn  The op to run.
	/// @param ctx Context pointer passed through to `fn`.
	/// @return true if it ran/queued.
	static bool run_or_inline(void (*fn)(void*), void* ctx);
};

/// @brief Single-flight coalesced dispatch onto the storage `Owner`.
///
/// At most one dispatch from a given `Coalescer` runs at a time: a `request()` made while
/// a dispatch is still in flight is dropped (the running one covers the demand). The
/// dispatched `fill` runs on the owner (fire-and-forget on Embassy, inline on legacy/host),
/// and the `Coalescer` releases itself once `fill` returns. Purpose: a high-frequency caller
/// (e.g. the recorder card-write drain) collapses onto a single owner op instead of
/// flooding the worker fiber's bounded queue.
///
/// @note All calls are expected from the main executor (not cross-thread — the requesters and
/// the fiber that runs `fill` both live there); the atomic guards re-entrancy (a `request()`
/// issued from inside a running `fill` coalesces).
class Coalescer {
public:
	/// @brief Construct a Coalescer.
	/// @param sd_routine when true, `request()` dispatches via `Owner::run_sd_routine`
	/// (SD-routine-class exclusion); when false (default), via `Owner::run`.
	explicit Coalescer(bool sd_routine = false) : sd_routine_(sd_routine) {}

	/// @brief If no dispatch from this `Coalescer` is in flight, run `fill(ctx)` on the owner;
	/// otherwise coalesce (no-op).
	/// @param fill The op to run.
	/// @param ctx  Context pointer passed through to `fill`.
	void request(void (*fill)(void*), void* ctx);

private:
	static void run_and_release(void* self);
	const bool sd_routine_;
	std::atomic<bool> in_flight_{false};
	void (*fill_)(void*) = nullptr;
	void* ctx_ = nullptr;
};

} // namespace deluge::storage
