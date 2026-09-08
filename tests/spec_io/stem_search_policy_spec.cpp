// tests/spec_io/stem_search_policy_spec.cpp
#include "processing/stem_export/stem_search_policy.h"

#include "libdeluge/file_write.h"

#include "cppspec.hpp"

using deluge::storage::kMaxStemCandidates;
using deluge::storage::nextStemSearchPhase;
using deluge::storage::Presence;
using deluge::storage::StemSearchInputs;
using deluge::storage::StemSearchPhase;

namespace {
StemSearchInputs probe(Presence p, int32_t tried) {
	return StemSearchInputs{StemSearchPhase::ProbeCandidate, DELUGE_FILE_WRITE_READY, p, tried};
}
} // namespace

// clang-format off
describe stem_search_policy("the stem folder search never reuses a folder it cannot rule out", $ {

	it("creates the base folder, then the song folder, then starts probing", _ {
		StemSearchInputs in{StemSearchPhase::MkdirBase, DELUGE_FILE_WRITE_READY, Presence::Absent, 0};
		expect(nextStemSearchPhase(in) == StemSearchPhase::MkdirSong).to_equal(true);
		in.phase = StemSearchPhase::MkdirSong;
		expect(nextStemSearchPhase(in) == StemSearchPhase::ProbeCandidate).to_equal(true);
	});

	// PROBE-THEN-CREATE, and this is the whole reason the probe exists. efatfs mkdir is idempotent
	// and succeeds on an existing directory, so a successful mkdir does NOT mean the folder was new.
	// Only a folder proven Absent is safe to take.
	it("creates only a candidate proven absent", _ {
		expect(nextStemSearchPhase(probe(Presence::Absent, 0)) == StemSearchPhase::MkdirCandidate).to_equal(true);
	});

	it("advances to the next candidate when one is occupied", _ {
		expect(nextStemSearchPhase(probe(Presence::Present, 0)) == StemSearchPhase::ProbeCandidate).to_equal(true);
	});

	// Undeterminable is NOT "free". A persistent BUSY must not spin, and must never risk overwriting
	// somebody's export.
	it("aborts rather than taking a candidate it could not rule out", _ {
		expect(nextStemSearchPhase(probe(Presence::Undeterminable, 0)) == StemSearchPhase::Failed).to_equal(true);
	});

	// The bound this rung adds. The loop was previously unbounded.
	it("gives up after the candidate ceiling rather than searching forever", _ {
		expect(nextStemSearchPhase(probe(Presence::Present, kMaxStemCandidates - 1))
		       == StemSearchPhase::ProbeCandidate).to_equal(true);
		expect(nextStemSearchPhase(probe(Presence::Present, kMaxStemCandidates))
		       == StemSearchPhase::Failed).to_equal(true);
	});

	it("finishes once the chosen candidate is created", _ {
		StemSearchInputs in{StemSearchPhase::MkdirCandidate, DELUGE_FILE_WRITE_READY, Presence::Absent, 3};
		expect(nextStemSearchPhase(in) == StemSearchPhase::Done).to_equal(true);
	});

	// A creation that fails after the probe said Absent means somebody else took it in between, or
	// the card turned hostile. Either way, do not silently reuse it -- fail.
	it("fails when the chosen candidate cannot be created", _ {
		StemSearchInputs in{StemSearchPhase::MkdirCandidate, DELUGE_FILE_WRITE_FAILED, Presence::Absent, 3};
		expect(nextStemSearchPhase(in) == StemSearchPhase::Failed).to_equal(true);
	});

	it("stays put while an operation is pending, and re-issues on UNKNOWN", _ {
		StemSearchInputs pending{StemSearchPhase::ProbeCandidate, DELUGE_FILE_WRITE_PENDING, Presence::Absent, 0};
		expect(nextStemSearchPhase(pending) == StemSearchPhase::ProbeCandidate).to_equal(true);
		StemSearchInputs unknown{StemSearchPhase::MkdirBase, DELUGE_FILE_WRITE_UNKNOWN, Presence::Absent, 0};
		expect(nextStemSearchPhase(unknown) == StemSearchPhase::MkdirBase).to_equal(true);
	});

	it("is terminal at Done and Failed", _ {
		StemSearchInputs d{StemSearchPhase::Done, DELUGE_FILE_WRITE_READY, Presence::Absent, 0};
		expect(nextStemSearchPhase(d) == StemSearchPhase::Done).to_equal(true);
		StemSearchInputs f{StemSearchPhase::Failed, DELUGE_FILE_WRITE_READY, Presence::Absent, 0};
		expect(nextStemSearchPhase(f) == StemSearchPhase::Failed).to_equal(true);
	});
});
// clang-format on

CPPSPEC_SPEC(stem_search_policy)
