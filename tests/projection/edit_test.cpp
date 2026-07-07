// Unit harness for the Edit substrate (M1a/M1b, docs/dev/project_model_migration_plan.md):
// dispatcher wiring, then (Task 3+) the SetParamValue pilot, its Consequence production,
// coalescence, and undo. Runs in the sim environment (real UnpatchedParamSet + ActionLogger +
// host allocator) on x86 (build-sim) and ARM (build-sim-arm via qemu).
//
// Exit: 0 = all checks pass, 1 = a failure.

#include "definitions_cxx.hpp"
#include "model/edits/edit.h"
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

// Pins the dispatcher contract: apply() runs the edit exactly once and passes its result through.
struct FakeEdit final : deluge::edits::Edit {
	int32_t applied = 0;
	Error result = Error::NONE;
	Error apply(deluge::edits::Context&) override {
		applied++;
		return result;
	}
};

void testDispatcher(deluge::edits::Context& ctx) {
	{
		FakeEdit edit;
		check(deluge::edits::apply(ctx, edit) == Error::NONE, "dispatcher: success passes through");
		check(edit.applied == 1, "dispatcher: edit applied exactly once");
	}
	{
		FakeEdit edit;
		edit.result = Error::UNSPECIFIED;
		check(deluge::edits::apply(ctx, edit) == Error::UNSPECIFIED, "dispatcher: error passes through");
		check(edit.applied == 1, "dispatcher: failing edit still applied exactly once");
	}
}

} // namespace

// Song construction (Task 3 fills this in properly); for Task 1 the Context members are only
// stored, never dereferenced by FakeEdit, but Context takes references so we need a real Song.
#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/song/song.h"

int main() {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	currentSong = song;

	deluge::edits::Context ctx{*song, song->project};
	testDispatcher(ctx);

	if (failures == 0) {
		std::fprintf(stdout, "edit_test: OK\n");
	}
	return failures ? 1 : 0;
}
