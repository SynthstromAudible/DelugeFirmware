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

// The send and the volumes: effects::SendAndVolume, the adapter the chains run, over Mono<SendAndVolume>, against the
// frozen send-and-volume over seeded full-scale frames cut into blocks of every size, the post-reverb volume stepping
// through Q31 so the ramp moves, across the volumes, the send (none, positive, negative and full), pan, ramp and
// stereo, forgetting and priming as a sound does; then the constructor's volume, an unset volume never primed, a prime
// after a block whose volume was the unset value, and the ramp's named quirk on the kernel itself.

#include "dsp-next/fixed/vca/send_and_volume.h"

#include "dsp-next/core/mono.h"
#include "effects/send_and_volume.h"
#include "effects_workloads.h"
#include "reference_effects.h"

#include "dsp-next/core/buffer_view.h"
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace effects_workloads;
using deluge::dsp::test::RawFrame;
using deluge::dsp::test::typed_frames;
using Kernel = deluge::dsp::fixed::SendAndVolume;
using Adapter = deluge::effects::SendAndVolume;

namespace {

static_assert(processor_kernel<Kernel>);
static_assert(moded_kernel<Kernel>);
static_assert(!std::is_polymorphic_v<Kernel>);

constexpr std::size_t kFrames = 2048;
/// The sends compared: none, a quarter, a negative one and both extremes.
constexpr std::array<int32_t, 5> kSends{0, 0x20000000, -0x30000000, std::numeric_limits<int32_t>::max(),
                                        std::numeric_limits<int32_t>::min()};

/// Runs @p ref and @p live over the same frames, one block's params from @p params_at(i): whether the frames, reverb
/// inputs and sends are equal.
template <typename ParamsAt>
bool compare(reference::SendAndVolume& ref, Adapter& live, std::size_t frames_n, uint32_t seed, ParamsAt params_at,
             bool forget_and_prime) {
	std::vector<RawFrame> a = frames(frames_n, seed);
	std::vector<RawFrame> b = a;
	std::vector<int32_t> ra(frames_n, 0x1234);
	std::vector<int32_t> rb = ra;
	bool sends_equal = true;
	std::size_t at = 0;
	for (std::size_t i = 0; at < a.size(); ++i) {
		const std::size_t n = block_at(i, a.size() - at);
		const reference::SendAndVolumeParams p = params_at(i);
		if (forget_and_prime && i % 11 == 7) {
			ref.forget();
			live.forget();
		}
		ref.prime(p.post_reverb_volume >> 1);
		live.prime(deluge::dsp::Q<27>::from_raw(p.post_reverb_volume >> 1));
		const bool sa = ref.process(std::span{a}.subspan(at, n), std::span{ra}.subspan(at, n), p);
		const bool sb = live.process(typed_frames(std::span{b}.subspan(at, n)),
		                             deluge::dsp::typed_view(std::span{rb}.subspan(at, n)),
		                             {.post_fx_volume = deluge::dsp::Q<27>::from_raw(p.post_fx_volume),
		                              .post_reverb_volume = deluge::dsp::Q<27>::from_raw(p.post_reverb_volume),
		                              .reverb_send = deluge::dsp::Q<31>::from_raw(p.reverb_send),
		                              .pan = deluge::dsp::Q<31>::from_raw(p.pan),
		                              .ramp = p.ramp,
		                              .stereo = p.stereo});
		sends_equal = sends_equal && sa == sb;
		at += n;
	}
	return sends_equal && same(a, b) && ra == rb;
}

} // namespace

// clang-format off
describe send_and_volume_kernel_spec("the send and the post-FX and post-reverb volumes as a kernel", ${
	it("equals the frozen send and volume across its params", _ {
		for (const int32_t post_fx_volume : {1 << 27, 0x7FFFFFFF}) {
			for (const int32_t send : kSends) {
				for (const int32_t pan : kSweep) {
					for (const bool ramp : {false, true}) {
						for (const bool stereo : {false, true}) {
							reference::SendAndVolume ref{0x01000000};
							Adapter live{0x01000000};
							expect(compare(ref, live, kFrames, 0x5E4D0001, [&](std::size_t i) {
								return reference::SendAndVolumeParams{.post_fx_volume = post_fx_volume,
								                                      .post_reverb_volume = kSweep[i % kSweep.size()],
								                                      .reverb_send = send,
								                                      .pan = pan,
								                                      .ramp = ramp,
								                                      .stereo = stereo};
							}, true)).to_be_true();
						}
					}
				}
			}
		}
	});

	it("ramps from the constructor's volume, set or unset", _ {
		for (const int32_t start : {0, 0x01000000, -1}) {
			reference::SendAndVolume ref{start};
			Adapter live{start};
			expect(compare(ref, live, 33, 0x5E4D0004, [](std::size_t) {
				return reference::SendAndVolumeParams{.post_fx_volume = 1 << 27, .post_reverb_volume = 0x04000000,
				                                      .reverb_send = 0, .pan = 0x20000000, .ramp = true,
				                                      .stereo = true};
			}, false)).to_be_true();
		}
	});

	it("ramps from -1 when forgotten and never primed, as the unset volume does", _ {
		reference::SendAndVolume ref{0x01000000};
		Adapter live{0x01000000};
		ref.forget();
		live.forget();
		std::vector<RawFrame> a = frames(33, 0x5E4D0003);
		std::vector<RawFrame> b = a;
		std::vector<int32_t> ra(33);
		std::vector<int32_t> rb(33);
		(void)ref.process(a, ra, {.post_fx_volume = 1 << 27, .post_reverb_volume = 0x04000000, .ramp = true});
		(void)live.process(typed_frames(std::span{b}), deluge::dsp::typed_view(std::span{rb}), {.post_fx_volume = deluge::dsp::Q<27>::one(), .post_reverb_volume = deluge::dsp::Q<27>::from_raw(0x04000000), .ramp = true});
		expect(same(a, b)).to_be_true();
	});

	it("primes after a block whose volume was the unset value, as the sentinel did", _ {
		reference::SendAndVolume ref{0x01000000};
		Adapter live{0x01000000};
		expect(compare(ref, live, 66, 0x5E4D0005, [](std::size_t i) {
			return reference::SendAndVolumeParams{.post_fx_volume = 1 << 27,
			                                      .post_reverb_volume = i == 0 ? -1 : 0x04000000,
			                                      .reverb_send = 0x20000000, .pan = 0, .ramp = true, .stereo = true};
		}, false)).to_be_true();
	});

	it("ends one block's change past the target when ramping (the named quirk)", _ {
		Mono<Kernel> k;
		k.on<&Kernel::start_from>(Kernel::Volume{});
		k.configure({.post_fx_volume = Kernel::Volume::from_raw(1 << 27),
		             .post_reverb_volume = Kernel::Volume::from_raw(1 << 27),
		             .pan = deluge::dsp::Q<31>{},
		             .ramp = true,
		             .stereo = true});
		std::vector<StereoSample> ones(4, StereoSample::from_mono(deluge::dsp::Q<31>::from_raw(1 << 30)));
		k.render_block(ones, ones);
		// The gain at this block's volume, held: what a ramp ending on its target would give at its last frame.
		using Volume = Kernel::Volume;
		const Volume held_gain = mul_as_wrap<Volume>(Volume::from_raw(1 << 27), Volume::from_raw(1 << 27));
		const q31_t at_target = mul_as_wrap<deluge::dsp::Q<31>>(deluge::dsp::Q<31>::from_raw(1 << 30), held_gain).raw();
		expect(ones[0].l.raw() > at_target).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(send_and_volume_kernel_spec);
