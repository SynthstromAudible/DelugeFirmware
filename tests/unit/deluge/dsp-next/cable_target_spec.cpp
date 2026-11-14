/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// synth::CableTarget's word operations against a verbatim copy of the runtime's ParamDescriptor, over every word a
// param and one, two or three sources make, and no target.

#include "param_descriptor_reference.h"

#include "synth/modulation/cable.h"

#include <cppspec.hpp>
#include <cstdint>

using deluge::synth::CableTarget;
using deluge::synth::PatchSource;
using Reference = param_descriptor_reference::ParamDescriptorReference;

namespace {

constexpr uint32_t kSources = static_cast<uint32_t>(PatchSource::NONE) + 1; // NONE included: files can name it

/// Every word: each param alone, with one source, with two, with three (the outermost from a spread), and no target.
template <class F>
void each_word(F f) {
	f(0xFFFF'FFFFu);
	for (uint32_t p = 0; p < 256; ++p) {
		f(p | 0xFFFF'FF00u);
		for (uint32_t s1 = 0; s1 < kSources; ++s1) {
			f(p | s1 << 8 | 0xFFFF'0000u);
			for (uint32_t s2 = 0; s2 < kSources; ++s2) {
				f(p | s1 << 8 | s2 << 16 | 0xFF00'0000u);
				for (uint32_t s3 : {0u, 7u, kSources - 1}) {
					f(p | s1 << 8 | s2 << 16 | s3 << 24);
				}
			}
		}
	}
}

/// The ways CableTarget{word} reads differently from the reference holding the same word.
int32_t read_mismatches(uint32_t word) {
	const CableTarget t{word};
	Reference r{static_cast<int32_t>(word)};
	int32_t bad = 0;
	bad += t.is_null() != r.isNull();
	bad += t.is_param() != r.isJustAParam();
	bad += t.param_index() != r.getJustTheParam();
	bad += t.top_source() != r.getTopLevelSource();
	bad += t.bottom_source() != r.getBottomLevelSource();
	bad += t.has_just_one_source() != r.hasJustOneSource();
	bad += t.has_second_source() != r.hasSecondSource();
	bad += t.second_source_from_top() != r.getSecondSourceFromTop();
	bad += t.modulated().word != r.getDestination().data;
	for (uint32_t p : {0u, 1u, 54u, 89u, 120u, 255u}) {
		bad +=
		    (t == CableTarget::param(static_cast<uint8_t>(p))) != r.isSetToParamWithNoSource(static_cast<int32_t>(p));
		Reference changed = r;
		changed.changeParam(static_cast<int32_t>(p));
		bad += t.with_param(static_cast<uint8_t>(p)).word != changed.data;
	}
	for (uint32_t s = 0; s < kSources; ++s) {
		Reference added = r;
		added.addSource(static_cast<PatchSource>(s));
		bad += t.add_source(static_cast<PatchSource>(s)).word != added.data;
	}
	return bad;
}

} // namespace

// clang-format off
describe cable_target_spec("synth::CableTarget", ${
	it("builds the words the reference's setters build", _ {
		int32_t bad = 0;
		for (uint32_t p = 0; p < 256; ++p) {
			Reference r;
			r.setToHaveParamOnly(static_cast<int32_t>(p));
			bad += CableTarget::param(static_cast<uint8_t>(p)).word != r.data;
			for (uint32_t s = 0; s < kSources; ++s) {
				r.setToHaveParamAndSource(static_cast<int32_t>(p), static_cast<PatchSource>(s));
				bad += CableTarget::depth_of(static_cast<uint8_t>(p), static_cast<PatchSource>(s)).word != r.data;
				for (uint32_t s_lowest = 0; s_lowest < kSources; ++s_lowest) {
					r.setToHaveParamAndTwoSources(static_cast<int32_t>(p), static_cast<PatchSource>(s),
					                              static_cast<PatchSource>(s_lowest));
					bad += CableTarget::depth_of_depth(static_cast<uint8_t>(p), static_cast<PatchSource>(s),
					                                   static_cast<PatchSource>(s_lowest))
					           .word
					       != r.data;
				}
			}
		}
		Reference null;
		null.setToNull();
		bad += CableTarget{}.word != null.data;
		expect(bad).to_equal(0);
	});
	it("reads and derives every word as the reference does", _ {
		int32_t bad = 0;
		each_word([&](uint32_t word) { bad += read_mismatches(word); });
		expect(bad).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(cable_target_spec);
