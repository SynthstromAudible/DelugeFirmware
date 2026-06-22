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

#ifdef __cplusplus
extern "C" {
#endif

/// Queue `fn(ctx)` to run on the cooperative worker. Operations are serialized.
/// `fn` may `yield()` at any call depth; the launching caller returns at once.
void deluge_worker_run(void (*fn)(void*), void* ctx);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_WORKER_H
