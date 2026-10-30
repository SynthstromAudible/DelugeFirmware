#include "instruments/deluge/mod_matrix.h"

#include <cppspec.hpp>

using namespace deluge::instrument;

namespace {
constexpr CableTarget kCutoff = CableTarget::param(24);
constexpr CableTarget kPitch = CableTarget::param(25);

CableRecords two_cables() {
	CableRecords r;
	r.append({PatchSource::ENVELOPE_0, kCutoff, CablePolarity::BIPOLAR});
	r.append({PatchSource::VELOCITY, kPitch, CablePolarity::UNIPOLAR});
	return r;
}
} // namespace

// clang-format off
describe mod_matrix("the Deluge instrument's cable-editing extension", ${
	it("adds a cable once, and never one with no target", _ {
		const CableRecords r = two_cables();
		const auto add = begin_add(r, {PatchSource::RANDOM, kCutoff, CablePolarity::BIPOLAR});
		expect(add.has_value()).to_equal(true);
		expect(add->cable().source).to_equal(PatchSource::RANDOM);
		expect(add->records_before == r).to_equal(true);
		expect(begin_add(r, {PatchSource::ENVELOPE_0, kCutoff, CablePolarity::UNIPOLAR}).has_value()).to_equal(false);
		expect(begin_add(r, {PatchSource::RANDOM, CableTarget{}, CablePolarity::BIPOLAR}).has_value()).to_equal(false);
	});

	it("refuses an add when the cables are full", _ {
		CableRecords r;
		for (uint8_t p = 0; p < deluge::synth::kMaxCables; ++p) {
			r.append({PatchSource::X, CableTarget::param(p), CablePolarity::BIPOLAR});
		}
		expect(begin_add(r, {PatchSource::Y, kCutoff, CablePolarity::BIPOLAR}).has_value()).to_equal(false);
	});

	it("records the cable an edit changes, before and after", _ {
		const CableRecords r = two_cables();
		const auto polarity = begin_set_polarity(r, 1, CablePolarity::BIPOLAR);
		expect(polarity->before.polarity).to_equal(CablePolarity::UNIPOLAR);
		expect(polarity->after.polarity).to_equal(CablePolarity::BIPOLAR);
		const auto reroute = begin_reroute(r, 0, kPitch);
		expect(reroute->after.target).to_equal(kPitch);
		expect(reroute->after.source).to_equal(PatchSource::ENVELOPE_0);
		expect(begin_reroute(r, 1, kPitch).has_value()).to_equal(false);
		expect(begin_remove(r, 2).has_value()).to_equal(false);
		expect(begin_remove(r, 1)->cable().source).to_equal(PatchSource::VELOCITY);
	});

	it("knows which side of an edit has its cable", _ {
		const CableRecords r = two_cables();
		const auto add = begin_add(r, {PatchSource::RANDOM, kCutoff, CablePolarity::BIPOLAR});
		expect(add->present_at(CableSide::BEFORE)).to_equal(false);
		expect(add->present_at(CableSide::AFTER)).to_equal(true);
		const auto remove = begin_remove(r, 0);
		expect(remove->present_at(CableSide::BEFORE)).to_equal(true);
		expect(remove->present_at(CableSide::AFTER)).to_equal(false);
		expect(begin_set_polarity(r, 0, CablePolarity::UNIPOLAR)->present_at(CableSide::AFTER)).to_equal(true);
	});

	it("creates a cable for a depth write only when it is missing", _ {
		const CableRecords r = two_cables();
		expect(needs_cable(r, PatchSource::RANDOM, kCutoff)).to_equal(true);
		expect(needs_cable(r, PatchSource::ENVELOPE_0, kCutoff)).to_equal(false);
		expect(needs_cable(r, PatchSource::RANDOM, CableTarget{})).to_equal(false);
	});

	it("deletes a cable at zero only on an active clip", _ {
		expect(settles(false, true)).to_equal(true);
		expect(settles(true, true)).to_equal(false);
		expect(settles(false, false)).to_equal(false);
	});
});
// clang-format on

CPPSPEC_SPEC(mod_matrix)
