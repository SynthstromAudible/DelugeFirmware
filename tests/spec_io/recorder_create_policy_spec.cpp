// tests/spec_io/recorder_create_policy_spec.cpp
#include "model/sample/recorder_create_policy.h"

#include "libdeluge/file_write.h"

#include "cppspec.hpp"

using deluge::storage::CreateInputs;
using deluge::storage::CreatePhase;
using deluge::storage::nextCreatePhase;

namespace {
CreateInputs at(CreatePhase p) {
	return CreateInputs{p, DELUGE_FILE_WRITE_UNKNOWN, false, false, false, false};
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

	it("is terminal at Done and Failed", _ {
		expect(nextCreatePhase(at(CreatePhase::Done)) == CreatePhase::Done).to_equal(true);
		expect(nextCreatePhase(at(CreatePhase::Failed)) == CreatePhase::Failed).to_equal(true);
	});
});
// clang-format on

CPPSPEC_SPEC(recorder_create_policy)
