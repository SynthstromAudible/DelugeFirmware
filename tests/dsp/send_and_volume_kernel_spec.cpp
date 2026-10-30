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

// The send and the volumes: reverb_send ahead of Mono<SendAndVolume>, as the effects' adapter runs them, against the
// frozen send-and-volume over seeded full-scale frames cut into blocks of every size, the post-reverb volume stepping
// through q31 so the ramp moves, across the volumes, send, pan, ramp and stereo; then the events the owners send (the
// constructor's volume, forget and prime) and the ramp's named quirk.

#include "dsp-next/fixed/vca/send_and_volume.h"

#include "dsp-next/core/mono.h"
#include "dsp-next/core/wrapping.h"
#include "effects_workloads.h"
#include "reference_effects.h"

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

static_assert(processor_kernel<SendAndVolume>);
static_assert(moded_kernel<SendAndVolume>);
static_assert(!std::is_polymorphic_v<SendAndVolume>);

constexpr std::size_t kFrames = 2048;

/// The adapter's block: the send as its own pass, then the gains. Returns whether anything was sent.
bool kernel_block(Mono<SendAndVolume>& gains, std::span<StereoSample> block, std::span<int32_t> reverb,
                  const reference::SendAndVolumeParams& p) {
	if (p.reverb_send != 0) {
		const int32_t send_gain = multiply_32x32_rshift32(p.post_fx_volume, p.reverb_send) << 5;
		for (std::size_t i = 0; i < block.size(); ++i) {
			reverb[i] = wrapping_add(reverb[i], reverb_send(block[i], send_gain));
		}
	}
	gains.configure({.post_fx_volume = p.post_fx_volume,
	                 .post_reverb_volume = p.post_reverb_volume,
	                 .pan = p.pan,
	                 .ramp = p.ramp,
	                 .stereo = p.stereo});
	gains.renderBlock(block, block);
	return p.reverb_send != 0;
}

} // namespace

// clang-format off
describe send_and_volume_kernel_spec("the send and the post-FX and post-reverb volumes as a kernel", ${
	it("equals the frozen send and volume across its params", _ {
		for (const int32_t post_fx_volume : {1 << 27, 0x7FFFFFFF}) {
			for (const int32_t send : {0, 0x20000000}) {
				for (const int32_t pan : kSweep) {
					for (const bool ramp : {false, true}) {
						for (const bool stereo : {false, true}) {
							reference::SendAndVolume ref{0x01000000};
							Mono<SendAndVolume> k;
							k.on<&SendAndVolume::start_from>(q31{0x01000000});
							std::vector<StereoSample> a = frames(kFrames, 0x5E4D0001);
							std::vector<StereoSample> b = a;
							std::vector<int32_t> ra(kFrames, 0x1234);
							std::vector<int32_t> rb = ra;
							std::size_t at = 0;
							for (std::size_t i = 0; at < a.size(); ++i) {
								const std::size_t n = block_at(i, a.size() - at);
								const reference::SendAndVolumeParams p{.post_fx_volume = post_fx_volume,
								                                       .post_reverb_volume = kSweep[i % kSweep.size()],
								                                       .reverb_send = send,
								                                       .pan = pan,
								                                       .ramp = ramp,
								                                       .stereo = stereo};
								if (i % 11 == 7) {
									ref.forget();
									k.on<&SendAndVolume::forget>();
									ref.prime(p.post_reverb_volume >> 1);
									k.on<&SendAndVolume::prime>(q31{p.post_reverb_volume >> 1});
								}
								const bool sa =
								    ref.process(std::span{a}.subspan(at, n), std::span{ra}.subspan(at, n), p);
								const bool sb =
								    kernel_block(k, std::span{b}.subspan(at, n), std::span{rb}.subspan(at, n), p);
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

	it("forgets, then ramps from the volume it is primed with, ignoring a second prime", _ {
		reference::SendAndVolume ref{0x01000000};
		Mono<SendAndVolume> k;
		k.on<&SendAndVolume::start_from>(q31{0x01000000});
		const reference::SendAndVolumeParams p{.post_fx_volume = 1 << 27, .post_reverb_volume = 1 << 27,
		                                       .reverb_send = 0, .pan = 0, .ramp = true, .stereo = true};
		ref.forget();
		ref.prime(0x02000000);
		ref.prime(0x03000000);
		k.on<&SendAndVolume::forget>();
		k.on<&SendAndVolume::prime>(q31{0x02000000});
		k.on<&SendAndVolume::prime>(q31{0x03000000});
		std::vector<StereoSample> a = frames(64, 0x5E4D0002);
		std::vector<StereoSample> b = a;
		std::vector<int32_t> ra(64);
		std::vector<int32_t> rb(64);
		(void)ref.process(a, ra, p);
		(void)kernel_block(k, b, rb, p);
		expect(same(a, b)).to_be_true();
	});

	it("ramps from -1 when forgotten and never primed, as the unset volume did", _ {
		reference::SendAndVolume ref{0x01000000};
		Mono<SendAndVolume> k;
		k.on<&SendAndVolume::start_from>(q31{0x01000000});
		const reference::SendAndVolumeParams p{.post_fx_volume = 1 << 27, .post_reverb_volume = 0x04000000,
		                                       .reverb_send = 0, .pan = 0, .ramp = true, .stereo = true};
		ref.forget();
		k.on<&SendAndVolume::forget>();
		std::vector<StereoSample> a = frames(33, 0x5E4D0003);
		std::vector<StereoSample> b = a;
		std::vector<int32_t> ra(33);
		std::vector<int32_t> rb(33);
		(void)ref.process(a, ra, p);
		(void)kernel_block(k, b, rb, p);
		expect(same(a, b)).to_be_true();
	});

	it("ramps from the constructor's volume on its first block", _ {
		for (const int32_t start : {0, 0x01000000, -1}) {
			reference::SendAndVolume ref{start};
			Mono<SendAndVolume> k;
			k.on<&SendAndVolume::start_from>(q31{start});
			const reference::SendAndVolumeParams p{.post_fx_volume = 1 << 27, .post_reverb_volume = 0x04000000,
			                                       .reverb_send = 0, .pan = 0x20000000, .ramp = true, .stereo = true};
			ref.prime(0x00800000);
			k.on<&SendAndVolume::prime>(q31{0x00800000});
			std::vector<StereoSample> a = frames(33, 0x5E4D0004);
			std::vector<StereoSample> b = a;
			std::vector<int32_t> ra(33);
			std::vector<int32_t> rb(33);
			(void)ref.process(a, ra, p);
			(void)kernel_block(k, b, rb, p);
			expect(same(a, b)).to_be_true();
		}
	});

	it("ends one block's change past the target when ramping (the named quirk)", _ {
		Mono<SendAndVolume> k;
		k.on<&SendAndVolume::start_from>(q31{0});
		k.configure({.post_fx_volume = 1 << 27, .post_reverb_volume = 1 << 27, .pan = 0, .ramp = true, .stereo = true});
		std::vector<StereoSample> ones(4, StereoSample{.l = 1 << 30, .r = 1 << 30});
		k.renderBlock(ones, ones);
		// The gain at this block's volume, held: what a ramp ending on its target would give at its last frame.
		const q31 held_gain = multiply_32x32_rshift32(1 << 27, 1 << 27) << 5;
		const q31 at_target = multiply_32x32_rshift32(1 << 30, held_gain) << 5;
		expect(ones[0].l > at_target).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(send_and_volume_kernel_spec);
