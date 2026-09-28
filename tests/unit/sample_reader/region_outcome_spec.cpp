// tests/unit/sample_reader/region_outcome_spec.cpp
//
// Pins RegionOutcome's mapping, including the reason the type exists at all: DelugeRegionState is a
// plain C enum whose READY is 1, so a boolean test on it silently inverts for UNAVAILABLE (== 3,
// truthy). The full note-on path that consumes this mapping needs a Voice/Sound/ModelStack and is
// not host-constructible, so this spec covers the piece that IS unit-testable and that the
// preview-E199 bug turned on.
#include "model/sample/sample_low_level_reader.h"

#include "cppspec.hpp"

describe region_outcome(
    "RegionOutcome", $ {
	    it(
	        "maps each port state to its own outcome", _ {
		        expect(regionOutcomeFrom(DELUGE_REGION_READY) == RegionOutcome::Ready).to_be_true();
		        expect(regionOutcomeFrom(DELUGE_REGION_LOADING) == RegionOutcome::Loading).to_be_true();
		        expect(regionOutcomeFrom(DELUGE_REGION_UNAVAILABLE) == RegionOutcome::Unavailable).to_be_true();
	        });

	    it(
	        "treats an unrecognised state as Unavailable, never as ready-or-waitable", _ {
		        RegionOutcome outcome = regionOutcomeFrom(static_cast<DelugeRegionState>(99));
		        expect(outcome == RegionOutcome::Unavailable).to_be_true();
		        // Spelled out because these are the two ways an unknown state could do damage: treating it as
		        // Ready plays from a region that was never acquired; treating it as Loading defers forever.
		        expect(outcome == RegionOutcome::Ready).to_be_false();
		        expect(outcome == RegionOutcome::Loading).to_be_false();
	        });

	    it(
	        "does not share the C enum's truthiness trap", _ {
		        // The bug this type prevents: `!DELUGE_REGION_UNAVAILABLE` is false, so a caller testing the
		        // raw state as a bool treats an unloadable region as success.
		        expect(!static_cast<int>(DELUGE_REGION_UNAVAILABLE)).to_be_false();
		        expect(RegionOutcome::Unavailable != RegionOutcome::Ready).to_be_true();
	        });

	    it(
	        "keeps Loading distinct from both success and failure", _ {
		        // Defer-not-drop depends on this three-way split surviving: if Loading ever collapsed into
		        // either neighbour, the note-on site would be back to a two-state decision.
		        expect(RegionOutcome::Loading != RegionOutcome::Ready).to_be_true();
		        expect(RegionOutcome::Loading != RegionOutcome::Unavailable).to_be_true();
	        });
    });

CPPSPEC_SPEC(region_outcome)
