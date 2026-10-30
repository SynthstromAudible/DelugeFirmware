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

// The metronome's click: Mono<Click>, skipped while it is not sounding as the metronome skips it, against the frozen
// metronome render over seeded full-scale frames cut into blocks of every size, at the bar's and the beat's pitch, at
// the menu's volumes and the song's, with a trigger mid-click and one after the click has stopped; whether it sounds
// after each block equals the frozen one's.

#include "dsp-next/fixed/osc/click.h"

#include "dsp-next/core/mono.h"
#include "effects_workloads.h"
#include "processing/metronome/metronome.h"
#include "reference_effects.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;
using namespace effects_workloads;

namespace {

static_assert(processor_kernel<Click>);
static_assert(!std::is_polymorphic_v<Click>);

constexpr std::size_t kFrames = 4096;
/// The bar's and the beat's phase increments, as the playback handler triggers them.
constexpr std::array<uint32_t, 2> kIncrements{128411753, 50960238};
/// The frames at which the click is triggered: the start, mid-click, and after a click has stopped.
constexpr std::array<std::size_t, 3> kTriggers{0, 700, 3000};

} // namespace

// clang-format off
describe click_spec("the metronome's click as a kernel", ${
	it("equals the frozen metronome at its pitches and volumes", _ {
		for (const uint32_t increment : kIncrements) {
			for (const int32_t menu : {0, 25, 50}) {
				const uint32_t volume = reference::Click::volume(menu);
				for (const int32_t post_fx : {ONE_Q31, 1 << 30, 0}) {
					reference::Click ref;
					Mono<Click> k;
					std::vector<StereoSample> a = frames(kFrames, 0xC11C0001);
					std::vector<StereoSample> b = a;
					std::size_t at = 0;
					std::size_t next_trigger = 0;
					for (std::size_t i = 0; at < a.size(); ++i) {
						const std::size_t n = block_at(i, a.size() - at);
						if (next_trigger < kTriggers.size() && at >= kTriggers[next_trigger]) {
							ref.trigger(increment);
							k.on<&Click::trigger>(increment);
							next_trigger++;
						}
						ref.render(std::span{a}.subspan(at, n), volume, post_fx);
						if (k.on<&Click::sounding>()) {
							k.configure({.level = multiply_32x32_rshift32(static_cast<int32_t>(volume), post_fx)});
							k.renderBlock(std::span{b}.subspan(at, n), std::span{b}.subspan(at, n));
						}
						expect(k.on<&Click::sounding>()).to_equal(ref.sounding);
						at += n;
					}
					expect(same(a, b)).to_be_true();
				}
			}
		}
	});

	it("is the metronome's click at the menu's volume", _ {
		for (const int32_t menu : {0, 7, 25, 50}) {
			reference::Click ref;
			Metronome metronome;
			metronome.setVolume(menu);
			std::vector<StereoSample> a = frames(1500, 0xC11C0002);
			std::vector<StereoSample> b = a;
			ref.trigger(kIncrements[0]);
			metronome.trigger(kIncrements[0]);
			for (std::size_t at = 0; at < a.size(); at += 128) {
				const std::size_t n = std::min<std::size_t>(128, a.size() - at);
				ref.render(std::span{a}.subspan(at, n), reference::Click::volume(menu), 1 << 30);
				metronome.render(std::span{b}.subspan(at, n), 1 << 30);
			}
			expect(same(a, b)).to_be_true();
			expect(metronome.sounding()).to_be_false();
		}
	});

	it("never opens its gate by rendering, before a trigger or after the gate closes: only a trigger opens it", _ {
		Mono<Click> k;
		k.configure({.level = 1 << 28});
		std::vector<StereoSample> silent(128, StereoSample{});
		k.renderBlock(silent, silent);
		expect(k.on<&Click::sounding>()).to_be_false();
		k.on<&Click::trigger>(kIncrements[0]);
		for (int i = 0; i < 8; ++i) {
			k.renderBlock(silent, silent);
		}
		expect(k.on<&Click::sounding>()).to_be_false();
		k.renderBlock(silent, silent);
		expect(k.on<&Click::sounding>()).to_be_false();
	});
});
// clang-format on

CPPSPEC_MAIN(click_spec);
