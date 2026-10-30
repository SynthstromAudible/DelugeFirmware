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

// The frozen effects reference against the live classes it was copied from: the bitcrush, the EQ and the send and
// volume, each run side by side with its copy over seeded full-scale frames cut into blocks of every size, across the
// params' range, with identical output and identical returned values. It proves the copy line for line before the
// kernels are held to it.

#include "reference_effects.h"

#include "effects_workloads.h"
#include "engines/effects/bitcrush.h"
#include "engines/effects/equalizer.h"
#include "engines/effects/send_and_volume.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

using namespace effects_workloads;

namespace {

constexpr std::size_t kFrames = 2048;

} // namespace

// clang-format off
describe reference_effects_spec("the frozen effects reference equals the live effects", ${
	it("bitcrush", _ {
		for (const int32_t crush : kSweep) {
			for (const int32_t srr : kSweep) {
				reference::Bitcrush ref;
				deluge::effects::Bitcrush live;
				std::vector<StereoSample> a = frames(kFrames, 0xB17C0001);
				std::vector<StereoSample> b = a;
				std::size_t at = 0;
				for (std::size_t i = 0; at < a.size(); ++i) {
					const std::size_t n = block_at(i, a.size() - at);
					// One block in eight with the reduction off, so it restarts.
					const int32_t block_srr = i % 8 == 5 ? std::numeric_limits<int32_t>::min() : srr;
					int32_t va = 1 << 27;
					int32_t vb = 1 << 27;
					ref.process(std::span{a}.subspan(at, n), {.bitcrush = crush, .sample_rate_reduction = block_srr},
					            va);
					vb >>= live.process(std::span{b}.subspan(at, n), {.bitcrush = crush, .sample_rate_reduction = block_srr})
					          .volume_shift;
					expect(va).to_equal(vb);
					at += n;
				}
				expect(same(a, b)).to_be_true();
			}
		}
	});

	it("equalizer", _ {
		const std::array<std::pair<int32_t, int32_t>, 3> kFrequencies{{
		    {0, 0}, {0x10000000, -0x10000000}, {std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()}}};
		for (const auto [bass_frequency, treble_frequency] : kFrequencies) {
			for (const int32_t bass : kSweep) {
				for (const int32_t treble : kSweep) {
					reference::Equalizer ref;
					deluge::effects::Equalizer live;
					std::vector<StereoSample> a = frames(kFrames, 0xE0E00001);
					std::vector<StereoSample> b = a;
					std::size_t at = 0;
					for (std::size_t i = 0; at < a.size(); ++i) {
						const std::size_t n = block_at(i, a.size() - at);
						// Each band flat one block in five, so it switches off and on.
						const int32_t block_bass = i % 5 == 1 ? 0 : bass;
						const int32_t block_treble = i % 5 == 3 ? 0 : treble;
						ref.process(std::span{a}.subspan(at, n), {.bass = block_bass, .treble = block_treble,
						                                          .bass_frequency = bass_frequency,
						                                          .treble_frequency = treble_frequency});
						live.process(std::span{b}.subspan(at, n), {.bass = block_bass, .treble = block_treble,
						                                           .bass_frequency = bass_frequency,
						                                           .treble_frequency = treble_frequency});
						at += n;
					}
					expect(same(a, b)).to_be_true();
				}
			}
		}
	});

	it("send and volume", _ {
		for (const int32_t post_fx_volume : {1 << 27, 0x7FFFFFFF}) {
			for (const int32_t send : {0, 0x20000000}) {
				for (const int32_t pan : kSweep) {
					for (const bool ramp : {false, true}) {
						for (const bool stereo : {false, true}) {
							reference::SendAndVolume ref{0x01000000};
							deluge::effects::SendAndVolume live{0x01000000};
							std::vector<StereoSample> a = frames(kFrames, 0x5E4D0001);
							std::vector<StereoSample> b = a;
							std::vector<int32_t> ra(kFrames, 0x1234);
							std::vector<int32_t> rb = ra;
							std::size_t at = 0;
							for (std::size_t i = 0; at < a.size(); ++i) {
								const std::size_t n = block_at(i, a.size() - at);
								// The post-reverb volume steps through the sweep, so the ramp moves.
								const int32_t post_reverb = kSweep[i % kSweep.size()];
								if (i % 11 == 7) {
									ref.forget();
									live.forget();
									ref.prime(post_reverb >> 1);
									live.prime(post_reverb >> 1);
								}
								const bool sa = ref.process(std::span{a}.subspan(at, n), std::span{ra}.subspan(at, n),
								                            {.post_fx_volume = post_fx_volume,
								                             .post_reverb_volume = post_reverb, .reverb_send = send,
								                             .pan = pan, .ramp = ramp, .stereo = stereo});
								const bool sb = live.process(std::span{b}.subspan(at, n), std::span{rb}.subspan(at, n),
								                             {.post_fx_volume = post_fx_volume,
								                              .post_reverb_volume = post_reverb, .reverb_send = send,
								                              .pan = pan, .ramp = ramp, .stereo = stereo});
								expect(sa).to_equal(sb);
								at += n;
							}
							expect(same(a, b)).to_be_true();
							expect(ra == rb).to_be_true();
						}
					}
				}
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(reference_effects_spec);
