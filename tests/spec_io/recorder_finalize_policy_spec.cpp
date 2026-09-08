// tests/spec_io/recorder_finalize_policy_spec.cpp
#include "model/sample/recorder_finalize_policy.h"

#include "libdeluge/file_write.h"

#include "cppspec.hpp"

using deluge::storage::FinalizeInputs;
using deluge::storage::FinalizePhase;
using deluge::storage::nextFinalizePhase;

namespace {
// A tick on which nothing is in flight: UNKNOWN is what a collect reports when no request is keyed.
FinalizeInputs at(FinalizePhase p) {
	return FinalizeInputs{p, DELUGE_FILE_WRITE_UNKNOWN, false, false, false, false, false, false};
}
FinalizeInputs ready(FinalizePhase p) {
	FinalizeInputs in = at(p);
	in.collected = DELUGE_FILE_WRITE_READY;
	return in;
}
} // namespace

// clang-format off
describe recorder_finalize_policy("finalize advances only on answers it actually got", $ {

	// A failed collect must reach Failed from EVERY phase. Falling through to the next phase would
	// finalize a file over a write that did not happen.
	it("reaches Failed from every I/O phase on a failed collect", _ {
		const FinalizePhase ioPhases[] = {
		    FinalizePhase::DrainClusters, FinalizePhase::WriteFinalPartial,
		    FinalizePhase::CloseBeforeAlter, FinalizePhase::OpenForAlter, FinalizePhase::HeaderRead,
		    FinalizePhase::HeaderWrite, FinalizePhase::BatchRead, FinalizePhase::BatchWrite,
		    FinalizePhase::Truncate, FinalizePhase::Close,
		};
		for (FinalizePhase p : ioPhases) {
			FinalizeInputs in = at(p);
			in.collected = DELUGE_FILE_WRITE_FAILED;
			expect(nextFinalizePhase(in) == FinalizePhase::Failed).to_equal(true);
		}
	});

	// PENDING holds position. Advancing on PENDING would issue the next operation while the previous
	// one still owns the caller's buffer.
	it("stays put while an operation is pending", _ {
		FinalizeInputs in = at(FinalizePhase::BatchRead);
		in.collected = DELUGE_FILE_WRITE_PENDING;
		expect(nextFinalizePhase(in) == FinalizePhase::BatchRead).to_equal(true);
	});

	it("drains clusters before writing the final partial one", _ {
		FinalizeInputs in = ready(FinalizePhase::DrainClusters);
		in.clustersRemain = true;
		expect(nextFinalizePhase(in) == FinalizePhase::DrainClusters).to_equal(true);
		in.clustersRemain = false;
		expect(nextFinalizePhase(in) == FinalizePhase::WriteFinalPartial).to_equal(true);
	});

	it("skips the final partial write when there is no buffer to write", _ {
		FinalizeInputs in = ready(FinalizePhase::WriteFinalPartial);
		in.havePartialBuffer = false;
		expect(nextFinalizePhase(in) == FinalizePhase::Decide).to_equal(true);
	});

	// The two branches diverge here and never rejoin until Bookkeeping.
	it("sends an altering finalize through close-then-open, and a plain one to truncate", _ {
		FinalizeInputs in = ready(FinalizePhase::Decide);
		in.altering = true;
		expect(nextFinalizePhase(in) == FinalizePhase::CloseBeforeAlter).to_equal(true);
		in.altering = false;
		in.needTruncate = true;
		expect(nextFinalizePhase(in) == FinalizePhase::Truncate).to_equal(true);
	});

	it("cycles BatchRead and BatchWrite while frames remain", _ {
		FinalizeInputs in = ready(FinalizePhase::BatchRead);
		in.altering = true;
		in.framesRemain = true;
		expect(nextFinalizePhase(in) == FinalizePhase::BatchWrite).to_equal(true);
		FinalizeInputs w = ready(FinalizePhase::BatchWrite);
		w.altering = true;
		w.framesRemain = true;
		expect(nextFinalizePhase(w) == FinalizePhase::BatchRead).to_equal(true);
	});

	// THE ordering ruling. The two branches truncate at opposite ends of their sequence, and neither
	// is arbitrary: the plain branch shortens the file before patching the header it just shortened,
	// while the altering branch cannot know its final length until its batches have run.
	it("truncates BEFORE the header when plain and AFTER the batches when altering", _ {
		FinalizeInputs plain = ready(FinalizePhase::Truncate);
		plain.altering = false;
		plain.needHeaderPatch = true;
		expect(nextFinalizePhase(plain) == FinalizePhase::HeaderRead).to_equal(true);

		FinalizeInputs alt = ready(FinalizePhase::BatchWrite);
		alt.altering = true;
		alt.framesRemain = false;
		alt.needTruncate = true;
		expect(nextFinalizePhase(alt) == FinalizePhase::Truncate).to_equal(true);
	});

	it("skips the header patch when nothing needs patching", _ {
		FinalizeInputs in = ready(FinalizePhase::Truncate);
		in.altering = false;
		in.needHeaderPatch = false;
		expect(nextFinalizePhase(in) == FinalizePhase::Close).to_equal(true);
	});

	it("ends at Done through Bookkeeping, never straight from Close", _ {
		expect(nextFinalizePhase(ready(FinalizePhase::Close)) == FinalizePhase::Bookkeeping).to_equal(true);
		expect(nextFinalizePhase(at(FinalizePhase::Bookkeeping)) == FinalizePhase::Done).to_equal(true);
	});

	// UNKNOWN means the request was never keyed -- re-issue by staying in the phase, NOT by advancing
	// as though it had completed.
	it("re-issues on UNKNOWN rather than advancing", _ {
		FinalizeInputs in = at(FinalizePhase::HeaderWrite);
		expect(nextFinalizePhase(in) == FinalizePhase::HeaderWrite).to_equal(true);
	});

	it("is terminal at Done and Failed", _ {
		expect(nextFinalizePhase(at(FinalizePhase::Done)) == FinalizePhase::Done).to_equal(true);
		expect(nextFinalizePhase(at(FinalizePhase::Failed)) == FinalizePhase::Failed).to_equal(true);
	});
});
// clang-format on

CPPSPEC_SPEC(recorder_finalize_policy)
