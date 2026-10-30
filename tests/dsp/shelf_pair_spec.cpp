/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// The EQ's shelves: effects::Equalizer, the adapter the chains run over the ShelfPair kernel, against the frozen EQ
// over seeded full-scale frames cut into blocks of every size, across the bands' and frequencies' range with each band
// switched flat and back mid-run; then a flat band's memory holding still, and the frozen EQ's held coefficients shown
// to be dead (a clone given them renders as a fresh one), which is why the kernel holds none.

#include "dsp-next/fixed/filter/shelf_pair.h"

#include "effects/equalizer.h"
#include "effects_workloads.h"
#include "reference_effects.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;
using namespace effects_workloads;

namespace {

static_assert(processor_kernel<ShelfPair>);
static_assert(moded_kernel<ShelfPair>);
static_assert(!std::is_polymorphic_v<ShelfPair>);
static_assert(ShelfPair::flat({}));
static_assert(!ShelfPair::flat({.bass = 1}));
static_assert(!ShelfPair::flat({.treble = -1}));

constexpr std::size_t kFrames = 2048;

reference::EqualizerParams as_reference(const ShelfPair::Settings& s) {
	return {
	    .bass = s.bass, .treble = s.treble, .bass_frequency = s.bass_frequency, .treble_frequency = s.treble_frequency};
}

} // namespace

// clang-format off
describe shelf_pair_spec("the EQ's bass and treble shelves as a kernel", ${
	it("equals the frozen EQ across its bands and frequencies", _ {
		const std::array<std::pair<int32_t, int32_t>, 3> kFrequencies{{
		    {0, 0}, {0x10000000, -0x10000000}, {std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()}}};
		for (const auto [bass_frequency, treble_frequency] : kFrequencies) {
			for (const int32_t bass : kSweep) {
				for (const int32_t treble : kSweep) {
					reference::Equalizer ref;
					deluge::effects::Equalizer k;
					std::vector<StereoSample> a = frames(kFrames, 0xE0E00001);
					std::vector<StereoSample> b = a;
					std::size_t at = 0;
					for (std::size_t i = 0; at < a.size(); ++i) {
						const std::size_t n = block_at(i, a.size() - at);
						// Each band flat one block in five, so it switches off and on.
						const ShelfPair::Settings s{.bass = i % 5 == 1 ? 0 : bass,
						                            .treble = i % 5 == 3 ? 0 : treble,
						                            .bass_frequency = bass_frequency,
						                            .treble_frequency = treble_frequency};
						ref.process(std::span{a}.subspan(at, n), as_reference(s));
						k.process(std::span{b}.subspan(at, n), s);
						at += n;
					}
					expect(same(a, b)).to_be_true();
				}
			}
		}
	});

	it("leaves a flat band's memory where it was", _ {
		const std::array<ShelfPair::Settings, 3> phases{{
		    {.bass = 0, .treble = 0x30000000, .bass_frequency = 0x10000000, .treble_frequency = -0x10000000},
		    {.bass = 0x30000000, .treble = 0, .bass_frequency = 0x10000000, .treble_frequency = -0x10000000},
		    {.bass = 0, .treble = 0x30000000, .bass_frequency = 0x10000000, .treble_frequency = -0x10000000},
		}};
		reference::Equalizer ref;
		deluge::effects::Equalizer k;
		std::vector<StereoSample> a = frames(192, 0xE0E00002);
		std::vector<StereoSample> b = a;
		for (std::size_t i = 0; i < phases.size(); ++i) {
			ref.process(std::span{a}.subspan(i * 64, 64), as_reference(phases[i]));
			k.process(std::span{b}.subspan(i * 64, 64), phases[i]);
		}
		expect(same(a, b)).to_be_true();
	});

	it("needs no held coefficients: a clone given them renders as a fresh one", _ {
		const reference::EqualizerParams both{.bass = 0x30000000, .treble = 0x30000000, .bass_frequency = 0x10000000,
		                                      .treble_frequency = -0x10000000};
		const reference::EqualizerParams bass_only{.bass = 0x30000000, .treble = 0, .bass_frequency = 0x7000000,
		                                           .treble_frequency = 0};
		const reference::EqualizerParams treble_only{.bass = 0, .treble = -0x30000000, .bass_frequency = 0,
		                                             .treble_frequency = 0x7000000};
		reference::Equalizer original;
		std::vector<StereoSample> warm = frames(300, 0xE0E00003);
		original.process(warm, both);
		reference::Equalizer copied;
		copied.copy_coefficients_from(original);
		reference::Equalizer fresh;
		std::vector<StereoSample> a = frames(384, 0xE0E00004);
		std::vector<StereoSample> b = a;
		for (const auto& [at, p] : {std::pair{0, both}, std::pair{128, bass_only}, std::pair{256, treble_only}}) {
			copied.process(std::span{a}.subspan(at, 128), p);
			fresh.process(std::span{b}.subspan(at, 128), p);
		}
		expect(same(a, b)).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(shelf_pair_spec);
