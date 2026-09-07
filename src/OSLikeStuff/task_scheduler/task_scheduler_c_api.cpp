/*
 * Copyright © 2025 Mark Adams
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

#include "OSLikeStuff/scheduler_api.h"
#include "OSLikeStuff/task_scheduler/task_scheduler.h"
#include "libdeluge/storage_owner.h" // deluge_storage_on_owner — cooperative default (Embassy BSP overrides)
#include "libdeluge/storage_wait.h"
#include "libdeluge/system.h" // deluge_in_interrupt — the ISR guard, via the boundary
#include "libdeluge/worker.h" // deluge_worker_run — cooperative default (Embassy BSP overrides)
#include "resource_checker.h"

extern TaskManager taskManager;

// The slow-storage reentrancy flag, owned by the scheduler (the runtime concurrency layer).
// Set around a storage busy-wait yield by the hooks below; read by the app via
// isSDRoutineActive() to gate user actions. Was the app-global sdRoutineLock in deluge.cpp.
namespace {
bool sdRoutineActive = false;

// Test-only seam for the cooperative deluge_storage_fs_busy() default below. Production never
// sets this, so it is always false there and the default's real behaviour ("FatFS runs inline on
// the caller, so nothing else can be holding the filesystem") is unchanged. tests/unit/
// scheduler_tests.cpp #includes this .cpp directly (rather than linking it), which makes this
// anonymous-namespace flag directly settable by name from the test file — the same trick already
// used there for sdRoutineActive above (and formerly currentlyAccessingCard) — so
// Scheduler.fsBusyBlocksSdTask can drive the RESOURCE_SD ceiling through the real seam
// (resource_checker.h -> deluge_storage_fs_busy()) instead of a symbol the ceiling no longer reads.
bool fsBusyTestOverride = false;
} // namespace
extern "C" bool isSDRoutineActive() {
	return sdRoutineActive;
}

extern "C" {
void startTaskManager() {
	taskManager.start();
}
void ignoreForStats() {
	taskManager.ignoreForStats();
}
double getAverageRunTimeForTask(TaskID id) {
	return taskManager.getAverageRunTimeForTask(id);
}
double getAverageRunTimeforCurrentTask() {
	return taskManager.getAverageRunTimeForCurrentTask();
}

void setNextRunTimeforCurrentTask(double seconds) {
	taskManager.setNextRunTimeforCurrentTask(seconds);
}

int8_t addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime, double targetTimeBetweenCalls,
                        double maxTimeBetweenCalls, const char* name, ResourceID resources) {
	return taskManager.addRepeatingTask(task,
	                                    TaskSchedule{.priority = priority,
	                                                 .backOffPeriod = backOffTime,
	                                                 .targetInterval = targetTimeBetweenCalls,
	                                                 .maxInterval = maxTimeBetweenCalls},
	                                    name, ResourceChecker{resources});
}
int8_t addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name, ResourceID resources) {
	return taskManager.addOnceTask(task, priority, timeToWait, name, ResourceChecker{resources});
}

int8_t addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name,
                          ResourceID resources) {
	return taskManager.addConditionalTask(task, priority, condition, name, ResourceChecker{resources});
}

void yield(RunCondition until) {
	taskManager.yield(until);
}

bool yieldWithTimeout(RunCondition until, double timeout) {
	return taskManager.yield(until, timeout);
}

bool yieldToIdle(RunCondition until) {
	return taskManager.yield(until, 0, true);
}

// libdeluge/worker.h — cooperative default. On the non-Embassy BSPs there is no
// worker fiber: the operation runs inline on the caller's stack, and its internal
// yield()s drive the cooperative task manager exactly as before. The Embassy BSP
// supplies its own deluge_worker_run (Rust, runs the op on a stackful fiber); that
// definition wins at link time and this object is not pulled from the archive.
bool deluge_worker_run(void (*fn)(void*), void* ctx) {
	fn(ctx);
	return true; // ran inline — never dropped on the cooperative BSPs
}

// SD-routine-class dispatch. On the cooperative BSPs the op runs inline (same as
// deluge_worker_run) and the SD-routine hold is a no-op: run-to-completion means
// discardRecorder can't interleave a mid-flight cardRoutine, and the writes set
// sdRoutineActive via the storage-wait hooks as they always have. The hold is an
// Embassy-only construct (see src/bsp/rust/src/fiber.rs).
bool deluge_worker_run_sd_routine(void (*fn)(void*), void* ctx) {
	fn(ctx);
	return true;
}

// Cooperative/host default: run is inline, so the caller is never "on a separate
// worker" — always false. The Embassy BSP supplies its own definition (fiber.rs)
// that reports whether we are on the worker fiber; that wins at link time.
bool deluge_worker_on_worker(void) {
	return false;
}

// Cooperative/host default: there is no separate worker to have "started" - the inline caller has
// been standing in for the owner since process start. Matches deluge_worker_on_worker's own
// cooperative default of false, so a caller asserting `on_worker() || !worker_started()` (the
// Embassy boot-exception idiom - see fiber.rs's WORKER_STARTED) is trivially satisfied here rather
// than tripping on a distinction this BSP doesn't have. The Embassy BSP supplies its own definition
// (fiber.rs) that wins at link time.
//
// This is also why that assertion's guarantee is debug/host-only, not a property this BSP itself
// enforces: `assert()` compiles out under NDEBUG, and even where it doesn't, `on_owner()` is ALSO
// always false here (see deluge_worker_on_worker() above), so `on_owner() || !worker_started()` is
// vacuously true on every cooperative/host build regardless of what actually called in -- it proves
// nothing here, only on Embassy. A future change that made this return true (to give cooperative/host
// a real "has the owner run yet" answer) would make every one of those assertions fire spuriously the
// first time an interaction-tier caller reached one of load_instrument_preset_ui.cpp's readers on
// this BSP, since `on_owner()` staying always-false would then never satisfy the left-hand side.
bool deluge_worker_started(void) {
	return false;
}

// libdeluge/storage_owner.h — cooperative/host default: FatFS runs inline on the
// caller, so the caller is always the owner. Embassy overrides (fiber-only).
bool deluge_storage_on_owner(void) {
	return true;
}

// libdeluge/storage_owner.h — cooperative/C-host default: FatFS runs inline on the
// caller, so no other context can be holding the filesystem. Embassy overrides
// (true while the efatfs FS mutex is held). Always false in production — see
// fsBusyTestOverride above for the one exception, which only a unit test can reach.
bool deluge_storage_fs_busy(void) {
	return fsBusyTestOverride;
}

// Cooperative yield hooks for slow-storage busy-waits (the <libdeluge/storage_wait.h>
// contract). The HAL/BSP calls these while spinning on SD / SPI-flash / USB so the
// scheduler keeps audio + UI + USB alive. Relocated here from the application: they
// are pure concurrency logic (an ISR guard, a reentrancy lock, a scheduler yield) and
// name no app code. sdRoutineActive is the scheduler-owned reentrancy flag the app reads
// via isSDRoutineActive(); the ISR guard now goes through deluge_in_interrupt() (true inside an ISR).
bool yieldingRoutineWithTimeoutForSD(RunCondition until, double timeoutSeconds) {
	if (deluge_in_interrupt()) {
		return false;
	}
	auto timeNow = getSystemTime();
	// We lock this to prevent multiple entry. Otherwise a storage wait could re-enter itself
	// through the work it yields to: SD wait -> audio task -> USB -> SD wait.
	if (sdRoutineActive) {
		// busy wait - matches running sdroutine in a loop while checking for condition
		while (!until()) {
			if (getSystemTime() > timeNow + timeoutSeconds) {
				return false;
			}
		}
		return true;
	}
	sdRoutineActive = true;
	bool ret = yieldWithTimeout(until, timeoutSeconds);
	sdRoutineActive = false;
	return ret;
}

void yieldingRoutineForSD(RunCondition until) {
	if (deluge_in_interrupt()) {
		return;
	}

	// We lock this to prevent multiple entry. Otherwise a storage wait could re-enter itself
	// through the work it yields to: SD wait -> audio task -> USB -> SD wait.
	if (sdRoutineActive) {
		// busy wait - matches running sdroutine in a loop while checking for condition
		while (!until()) {
			asm volatile("nop");
		}
		return;
	}
	sdRoutineActive = true;
	yield(until);
	sdRoutineActive = false;
}

void removeTask(TaskID id) {
	taskManager.removeTask(id);
}

void boostTask(TaskID id) {
	taskManager.boostTask(id);
}

void runTask(TaskID id) {
	taskManager.runTask(id);
}

void blockTask(TaskID id) {
	taskManager.blockTask(id);
}

void unblockTask(TaskID id) {
	taskManager.unblockTask(id);
}

double getSystemTime() {
	return taskManager.getSecondsFromStart();
}
}
