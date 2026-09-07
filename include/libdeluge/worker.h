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

/// libdeluge/worker.h — run a long, cooperatively-yielding operation off the
/// caller's stack.
///
/// Some user-initiated operations (song load, stem export, grid clip create) run
/// for a long time and `yield()` (scheduler_api.h) partway through to let other
/// work proceed. On a runtime where the caller's context cannot itself suspend
/// (e.g. the Embassy BSP, where the operation is dispatched from a task that must
/// stay responsive), hand the operation to the worker: it runs on a dedicated
/// stackful fiber so its `yield()`s suspend the *operation*, not the caller.
///
/// `deluge_worker_run` returns immediately (the operation is queued); the launching
/// handler must therefore include any post-operation work *inside* `fn`.
#ifndef LIBDELUGE_WORKER_H
#define LIBDELUGE_WORKER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Queue @p fn(@p ctx) to run on the cooperative worker.
///
/// Operations are serialized. @p fn may yield() (scheduler_api.h) at any call depth; the
/// launching caller returns at once.
/// @param fn  The operation to run, invoked as fn(ctx).
/// @param ctx Opaque context passed through to @p fn.
/// @return true if the operation ran (cooperative/inline) or was queued (Embassy
///         worker); false if it was dropped because the worker queue was full and
///         will NOT run — a coalescing caller must treat false as "not dispatched".
bool deluge_worker_run(void (*fn)(void*), void* ctx);

/// @brief Like deluge_worker_run, but the operation is an SD-routine-class op.
///
/// For its whole in-flight window (enqueue → completion) the worker holds off
/// RESOURCE_SD_ROUTINE scheduler tasks, so a task that frees an object the op is mid-way through
/// (e.g. the recorder, freed by discardRecorder) cannot run concurrently with it. Cooperative/host:
/// identical to deluge_worker_run (inline). Embassy: increments an SD-routine hold at enqueue,
/// released at completion.
/// @param fn  The operation to run, invoked as fn(ctx).
/// @param ctx Opaque context passed through to @p fn.
/// @return As deluge_worker_run: true if it ran/queued, false if dropped (queue
///         full) and will NOT run — on false NOTHING was enqueued and no hold was taken.
bool deluge_worker_run_sd_routine(void (*fn)(void*), void* ctx);

/// @brief Whether the calling context IS the storage worker (the worker fiber on the Embassy BSP).
///
/// Lets a dual-context caller skip re-dispatching when it is already on the worker —
/// dispatching again would nest a queued op inside the running one (Embassy) and reorder or
/// deadlock. Cooperative/host: always false, because `deluge_worker_run` runs inline there, so
/// nesting is harmless and no caller needs to branch on this.
/// @return true if the calling context is already the storage worker; false otherwise (always
///         false on the cooperative/host build).
bool deluge_worker_on_worker(void);

/// @brief Whether the worker has ever run — i.e. whether an owner exists to be on at all.
///
/// Embassy: latched true the first time the worker pump runs (`app_task`/`host_app_task`'s first
/// poll); false only during the narrow pre-owner boot window, the synchronous `deluge_boot()` call
/// chain that runs before that pump starts. This is the same distinction `sd.rs`'s diskio asserts
/// draw (`on_fiber() || !worker_started()`) to tell the boot-time FatFS mount apart from a genuine
/// off-owner transfer at runtime — surfaced here so a C++ caller can draw it too, e.g. an assertion
/// that a recursive reader never runs off the owner except during that one legal boot exception.
/// Cooperative/host: always false, matching `deluge_worker_on_worker`'s own cooperative default —
/// there is no separate worker to have "started" and no caller needs to branch on this.
/// @return true iff the worker has run at least once (Embassy only; always false cooperative/host).
bool deluge_worker_started(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_WORKER_H
