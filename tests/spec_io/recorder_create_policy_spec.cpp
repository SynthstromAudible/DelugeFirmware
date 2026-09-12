// tests/spec_io/recorder_create_policy_spec.cpp
#include "model/sample/recorder_create_policy.h"
#include "model/sample/recorder_write_policy.h"

#include "libdeluge/file_write.h"

#include "cppspec.hpp"

using deluge::storage::CreateInputs;
using deluge::storage::CreatePhase;
using deluge::storage::kMaxUnproductiveTicks;
using deluge::storage::nextCreatePhase;

namespace {
CreateInputs at(CreatePhase p) {
	return CreateInputs{p, DELUGE_FILE_WRITE_UNKNOWN, false, false, false, false, 0, false};
}
CreateInputs ready(CreatePhase p) {
	CreateInputs in = at(p);
	in.collected = DELUGE_FILE_WRITE_READY;
	return in;
}
} // namespace

// clang-format off
describe recorder_create_policy("creating a recording's file advances only on answers it got", $ {

	it("stays put while an operation is pending", _ {
		CreateInputs in = at(CreatePhase::Open);
		in.collected = DELUGE_FILE_WRITE_PENDING;
		expect(nextCreatePhase(in) == CreatePhase::Open).to_equal(true);
	});

	// UNKNOWN means the request was never keyed: stay so the caller re-issues. Advancing here would
	// treat an un-issued request as a completed one.
	it("re-issues on UNKNOWN rather than advancing", _ {
		expect(nextCreatePhase(at(CreatePhase::Open)) == CreatePhase::Open).to_equal(true);
	});

	// A full card is not a channel failure -- it is an answer, and a terminal one.
	it("fails immediately when the card has no free clusters", _ {
		CreateInputs in = ready(CreatePhase::Stats);
		in.spaceExhausted = true;
		expect(nextCreatePhase(in) == CreatePhase::Failed).to_equal(true);
	});

	it("goes from Stats to the path search when there is space", _ {
		CreateInputs in = ready(CreatePhase::Stats);
		in.spaceExhausted = false;
		expect(nextCreatePhase(in) == CreatePhase::PathSearch).to_equal(true);
	});

	// The search spans ticks of its own; the create machine waits on it rather than racing ahead.
	it("waits in PathSearch until a path exists", _ {
		CreateInputs in = ready(CreatePhase::PathSearch);
		in.pathReady = false;
		expect(nextCreatePhase(in) == CreatePhase::PathSearch).to_equal(true);
		in.pathReady = true;
		expect(nextCreatePhase(in) == CreatePhase::Open).to_equal(true);
	});

	it("finishes when the open succeeds", _ {
		expect(nextCreatePhase(ready(CreatePhase::Open)) == CreatePhase::Done).to_equal(true);
	});

	// THE retry ruling. A missing parent folder is the one open failure worth acting on, and it is
	// worth acting on EXACTLY ONCE -- the old code's `triedCreatingRecordingFolder` flag said the
	// same thing through a goto.
	it("creates the parent folder once on NOT_FOUND, and only once", _ {
		CreateInputs in = at(CreatePhase::Open);
		in.collected = DELUGE_FILE_WRITE_FAILED;
		in.openNotFound = true;
		in.triedMakeFolder = false;
		expect(nextCreatePhase(in) == CreatePhase::MakeFolder).to_equal(true);

		in.triedMakeFolder = true;
		expect(nextCreatePhase(in) == CreatePhase::Failed).to_equal(true);
	});

	// Any OTHER open failure is terminal at once: creating a folder would not have helped.
	it("does not try a folder for an open failure that is not NOT_FOUND", _ {
		CreateInputs in = at(CreatePhase::Open);
		in.collected = DELUGE_FILE_WRITE_FAILED;
		in.openNotFound = false;
		expect(nextCreatePhase(in) == CreatePhase::Failed).to_equal(true);
	});

	it("retries the open after making the folder", _ {
		expect(nextCreatePhase(ready(CreatePhase::MakeFolder)) == CreatePhase::Open).to_equal(true);
	});

	// A folder that cannot be made is the end of it -- there is nothing else to try.
	it("fails when the folder cannot be made", _ {
		CreateInputs in = at(CreatePhase::MakeFolder);
		in.collected = DELUGE_FILE_WRITE_FAILED;
		expect(nextCreatePhase(in) == CreatePhase::Failed).to_equal(true);
	});

	// Refused, never keyed, or displaced: the caller asks again, and at the bound nothing is outstanding, so
	// the create fails.
	it("fails a refused or unkeyed operation at the bound, and not before", _ {
		CreateInputs opening = at(CreatePhase::Open);
		opening.unproductiveTicks = kMaxUnproductiveTicks - 1;
		expect(nextCreatePhase(opening) == CreatePhase::Open).to_equal(true);
		opening.unproductiveTicks = kMaxUnproductiveTicks;
		expect(nextCreatePhase(opening) == CreatePhase::Failed).to_equal(true);
		CreateInputs folder = at(CreatePhase::MakeFolder);
		folder.unproductiveTicks = kMaxUnproductiveTicks;
		expect(nextCreatePhase(folder) == CreatePhase::Failed).to_equal(true);
	});

	// A pending open may yet hand back a stream, so the machine waits for it however long it takes.
	it("waits for a pending answer even past the bound", _ {
		CreateInputs in = at(CreatePhase::Open);
		in.collected = DELUGE_FILE_WRITE_PENDING;
		in.unproductiveTicks = kMaxUnproductiveTicks + 100;
		in.failing = true;
		expect(nextCreatePhase(in) == CreatePhase::Open).to_equal(true);
	});

	// Fail now, release later: the answer to an operation that went past the bound ends the wait, not the
	// failure -- even a READY open, and even a NOT_FOUND that would otherwise earn the folder retry.
	it("fails on the terminal answer to an operation that went past the bound", _ {
		CreateInputs opened = ready(CreatePhase::Open);
		opened.failing = true;
		expect(nextCreatePhase(opened) == CreatePhase::Failed).to_equal(true);
		CreateInputs missing = at(CreatePhase::Open);
		missing.collected = DELUGE_FILE_WRITE_FAILED;
		missing.openNotFound = true;
		missing.failing = true;
		expect(nextCreatePhase(missing) == CreatePhase::Failed).to_equal(true);
	});

	it("is terminal at Done and Failed", _ {
		expect(nextCreatePhase(at(CreatePhase::Done)) == CreatePhase::Done).to_equal(true);
		expect(nextCreatePhase(at(CreatePhase::Failed)) == CreatePhase::Failed).to_equal(true);
	});
});
// clang-format on

CPPSPEC_SPEC(recorder_create_policy)
