// tests/spec_io/recorder_write_policy_spec.cpp
#include "model/sample/recorder_write_policy.h"

#include "libdeluge/file_write.h"

#include "cppspec.hpp"

using deluge::storage::kMaxPendingTicks;
using deluge::storage::nextWriteStep;
using deluge::storage::WriteStep;

// clang-format off
describe recorder_write_policy("the recorder's cluster write never waits for an answer nobody owes it", $ {

	// The channel-off build. Every other input is irrelevant; this must dominate, or a build with no
	// Rust driver would sit waiting for an answer nothing can produce.
	it("takes the synchronous arm when the channel is not running", _ {
		expect(nextWriteStep(false, false, DELUGE_FILE_WRITE_UNKNOWN, 0) == WriteStep::Inline).to_equal(true);
		expect(nextWriteStep(false, true, DELUGE_FILE_WRITE_PENDING, 0) == WriteStep::Inline).to_equal(true);
	});

	it("issues when the channel is live and nothing is in flight", _ {
		expect(nextWriteStep(true, false, DELUGE_FILE_WRITE_UNKNOWN, 0) == WriteStep::Issue).to_equal(true);
	});

	it("waits while a write is genuinely pending", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, 0) == WriteStep::Wait).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, kMaxPendingTicks - 1) == WriteStep::Wait).to_equal(true);
	});

	// The stall bound. A channel that accepts and never answers has stranded three consumers on this
	// project already; a recording must survive a driver that stops answering.
	it("abandons the channel when a pending write is never answered", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, kMaxPendingTicks) == WriteStep::Inline).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_PENDING, kMaxPendingTicks + 1) == WriteStep::Inline).to_equal(true);
	});

	it("completes on READY", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_READY, 0) == WriteStep::Complete).to_equal(true);
	});

	it("fails on FAILED", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_FAILED, 0) == WriteStep::Failed).to_equal(true);
	});

	// UNKNOWN means "request again", NOT "keep waiting". Conflating the two leaves the recorder
	// waiting for an answer to a request nothing ever accepted, and the recording stalls with no
	// error anywhere.
	it("re-issues on UNKNOWN rather than waiting", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_UNKNOWN, 0) == WriteStep::Issue).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_UNKNOWN, 0) == WriteStep::Wait).to_equal(false);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_UNKNOWN, kMaxPendingTicks - 1) == WriteStep::Issue).to_equal(true);
	});

	// A READY that arrives on the very tick the bound expires must still be honoured: the bytes ARE
	// on the card, and discarding that answer would rewrite the cluster from a buffer the completion
	// path is about to recycle.
	it("does not let the stall bound swallow a terminal answer", _ {
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_READY, kMaxPendingTicks + 50) == WriteStep::Complete).to_equal(true);
		expect(nextWriteStep(true, true, DELUGE_FILE_WRITE_FAILED, kMaxPendingTicks + 50) == WriteStep::Failed).to_equal(true);
	});
});
// clang-format on

CPPSPEC_SPEC(recorder_write_policy)
