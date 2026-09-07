// tests/spec_storage/mock_worker.cpp
//
// Host-only stand-in for include/libdeluge/worker.h's deluge_worker_run. The real
// definitions are the scheduler's cooperative default (task_scheduler_c_api.cpp,
// runs fn(ctx) inline) or the Embassy BSP's stackful-fiber dispatch (src/bsp/rust);
// neither is appropriate to drag into a host unit spec. This mirrors the cooperative
// default's inline semantics without pulling in the scheduler, matching the
// mock_file_io.cpp / mock_source.h pattern already used by the other spec dirs.
#include "libdeluge/worker.h"

// Test hook: when true, `deluge_worker_run` drops the op (does not run it) and returns
// false, modelling the Embassy worker queue being full. Lets specs exercise the
// Coalescer's drop-recovery (that it releases its single-flight guard on a dropped
// dispatch instead of wedging). Reset to false between tests that use it.
bool g_mock_worker_drop = false;

extern "C" bool deluge_worker_run(void (*fn)(void*), void* ctx) {
	if (g_mock_worker_drop) {
		return false; // dropped — the op does not run
	}
	fn(ctx);
	return true;
}

// Host stand-in: same inline semantics + drop hook as deluge_worker_run. The
// SD-routine hold is Embassy-only, so on host this is behaviourally identical.
extern "C" bool deluge_worker_run_sd_routine(void (*fn)(void*), void* ctx) {
	if (g_mock_worker_drop) {
		return false;
	}
	fn(ctx);
	return true;
}

// Host stand-in: same as the cooperative default (task_scheduler_c_api.cpp) — run
// is always inline here, so the caller is never "on a separate worker": always
// false. The Embassy BSP's fiber.rs supplies the real predicate.
extern "C" bool deluge_worker_on_worker(void) {
	return false;
}

// Host stand-in: same as the cooperative default (task_scheduler_c_api.cpp) — there is no
// separate worker to have "started", so always false. The Embassy BSP's fiber.rs supplies the
// real WORKER_STARTED-backed predicate.
extern "C" bool deluge_worker_started(void) {
	return false;
}
