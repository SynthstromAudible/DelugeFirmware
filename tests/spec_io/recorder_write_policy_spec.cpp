// tests/spec_io/recorder_write_policy_spec.cpp
#include "model/sample/recorder_write_policy.h"

#include "libdeluge/file_write.h"

#include "cppspec.hpp"

using deluge::storage::kMaxUnproductiveUs;
using deluge::storage::nextWriteStep;
using deluge::storage::stalledPastBound;
using deluge::storage::WriteStep;

// clang-format off
describe recorder_write_policy("the recorder's cluster write gives up without blocking and without freeing what the driver holds", $ {

	// The synchronous arm is for where blocking is legal. A usable channel never falls back to it, however
	// long the channel keeps the recorder waiting.
	it("takes the synchronous arm only when the channel cannot be used", _ {
		expect(nextWriteStep(false, false, DELUGE_FILE_WRITE_UNKNOWN, 0) == WriteStep::Inline).to_equal(true);
		expect(nextWriteStep(false, true, DELUGE_FILE_WRITE_PENDING, 0) == WriteStep::Inline).to_equal(true);
		expect(nextWriteStep(true, false, DELUGE_FILE_WRITE_UNKNOWN, kMaxUnproductiveUs + 5) == WriteStep::Inline).to_equal(false);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, kMaxUnproductiveUs + 5) == WriteStep::Inline).to_equal(false);
	});

	// A refused write is owed, not lost and not written here: ask again.
	it("asks again for an owed write below the bound", _ {
		expect(nextWriteStep(true, false, DELUGE_FILE_WRITE_UNKNOWN, 0) == WriteStep::Issue).to_equal(true);
		expect(nextWriteStep(true, false, DELUGE_FILE_WRITE_UNKNOWN, kMaxUnproductiveUs - 1) == WriteStep::Issue).to_equal(true);
	});

	// The channel never accepted it, so nothing is outstanding and failing releases nothing the driver holds.
	it("fails an owed write the channel keeps refusing", _ {
		expect(nextWriteStep(true, false, DELUGE_FILE_WRITE_UNKNOWN, kMaxUnproductiveUs) == WriteStep::Failed).to_equal(true);
	});

	it("waits while a write is pending below the bound", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, 0) == WriteStep::Wait).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, kMaxUnproductiveUs - 1) == WriteStep::Wait).to_equal(true);
	});

	// Fail now, release later. Past the bound the take has failed, but the driver was handed a pointer into
	// the cluster's buffer and may still write from it, so the buffer stays until a terminal answer.
	it("fails a write still pending past the bound, and keeps waiting for its answer", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, kMaxUnproductiveUs) == WriteStep::StalledWait).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, kMaxUnproductiveUs + 1000) == WriteStep::StalledWait).to_equal(true);
	});

	it("completes on READY", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_READY, 0) == WriteStep::Complete).to_equal(true);
	});

	it("fails on FAILED", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_FAILED, 0) == WriteStep::Failed).to_equal(true);
	});

	// UNKNOWN means "request again", NOT "keep waiting". Conflating the two leaves the recorder waiting for
	// an answer to a request nothing ever accepted. At the bound it fails: nothing is outstanding.
	it("re-issues on UNKNOWN rather than waiting, until the bound", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_UNKNOWN, 0) == WriteStep::Issue).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_UNKNOWN, 0) == WriteStep::Wait).to_equal(false);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_UNKNOWN, kMaxUnproductiveUs - 1) == WriteStep::Issue).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_UNKNOWN, kMaxUnproductiveUs) == WriteStep::Failed).to_equal(true);
	});

	// A READY that arrives past the bound must still be honoured: the bytes ARE on the card, and the answer
	// is what releases the buffer.
	it("does not let the bound swallow a terminal answer", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_READY, kMaxUnproductiveUs + 50) == WriteStep::Complete).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_FAILED, kMaxUnproductiveUs + 50) == WriteStep::Failed).to_equal(true);
	});

	// The latch the create and finalize machines share with this one.
	it("calls an operation stalled only while it is still pending past the bound", _ {
		expect(stalledPastBound(DELUGE_FILE_WRITE_PENDING, kMaxUnproductiveUs - 1)).to_equal(false);
		expect(stalledPastBound(DELUGE_FILE_WRITE_PENDING, kMaxUnproductiveUs)).to_equal(true);
		expect(stalledPastBound(DELUGE_FILE_WRITE_READY, kMaxUnproductiveUs + 1)).to_equal(false);
		expect(stalledPastBound(DELUGE_FILE_WRITE_FAILED, kMaxUnproductiveUs + 1)).to_equal(false);
		expect(stalledPastBound(DELUGE_FILE_WRITE_UNKNOWN, kMaxUnproductiveUs + 1)).to_equal(false);
	});
});
// clang-format on

CPPSPEC_SPEC(recorder_write_policy)
