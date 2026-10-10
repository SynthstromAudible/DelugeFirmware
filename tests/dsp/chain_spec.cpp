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

#include "dsp-next/core/chain.h"
#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/dynamics/duck_envelope.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/shaping/tanh_kernel.h"
#include "util/fixedpoint.h"

#include <argon.hpp>
#include <array>
#include <bit>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;

namespace {

using fixed::DuckEnvelope;
using fixed::SeededNoise;
using fixed::Tanh;
using Tracker = fixed::ShiftTracker<11>;
/// The shift tracker as a high-pass, its mode fixed at compile time.
using Remove = Pinned<Tracker, Tracker::Mode::REMOVE>;

/// A processor adding a fixed offset to each sample, counting its steps.
struct AddK {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 offset; ///< Q31.
	};
	struct Coefficients {
		q31 offset; ///< Q31.
	};
	struct State {
		int32_t steps; ///< How many samples this part has stepped.
	};
	static Coefficients coefficients(const Settings& s) { return {.offset = s.offset}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s, q31 x) {
		++s.steps;
		return wrapping_add(x, c.offset);
	}
};

/// Every prepare the logging kernels have run, in order, by their log ids.
std::vector<int> prepare_log;

/// A processor scaling each sample by a gain its prepare takes from the State: the base plus a sixteenth of the running
/// sum of its outputs' eighths. A part stepped with the wrong Block, or prepared on the wrong State, gives another
/// gain.
struct ScaleK {
	using value_type = q31;
	using step_type = q31;
	static constexpr int kLogId = 4; ///< What its prepare logs.
	struct Settings {
		q31 base; ///< The gain on a silent history, Q1.31 scaled ×2.
	};
	struct Coefficients {
		q31 base; ///< As set.
	};
	struct State {
		q31 acc;          ///< The running sum of each output's eighth.
		int32_t prepares; ///< How many blocks this part has prepared.
	};
	struct Block {
		q31 gain; ///< This block's gain, Q1.31 scaled ×2.
	};
	static Coefficients coefficients(const Settings& s) { return {.base = s.base}; }
	static Block prepare(const Coefficients& c, State& s, std::size_t /*n*/) {
		prepare_log.push_back(kLogId);
		++s.prepares;
		return {.gain = wrapping_add(c.base, s.acc >> 4)};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, State& s, q31 x) {
		const q31 y = multiply_32x32_rshift32(x, b.gain) << 1;
		s.acc = wrapping_add(s.acc, y >> 3);
		return y;
	}
};

/// A pass-through whose prepare logs @p Id.
template <int Id>
struct PrepLog {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) {
		prepare_log.push_back(Id);
		return {};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A generator counting up by its step, wrapping.
struct RampGen {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 by; ///< The increment per sample.
	};
	struct Coefficients {
		q31 by; ///< The increment per sample.
	};
	struct State {
		q31 v; ///< The last value.
	};
	static Coefficients coefficients(const Settings& s) { return {.by = s.by}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s) {
		s.v = wrapping_add(s.v, c.by);
		return s.v;
	}
};

/// A mixer with memory: a leaky sum of a minus half of b.
struct SumMix {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 acc; ///< The leaky sum.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s, q31 a, q31 b) {
		s.acc = wrapping_add(s.acc >> 1, wrapping_sub(a, b >> 1));
		return s.acc;
	}
};

/// A processor whose reset quirk starts its memory at 7, not 0.
struct QuirkK {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 z; ///< The running sum of each input's quarter.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void reset(State& s) { s.z = 7; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s, q31 x) {
		s.z = wrapping_add(s.z, x >> 2);
		return s.z;
	}
};

/// A staged kernel: a block-wise copy.
struct StagedK {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static void process(const Coefficients& /*c*/, State& /*s*/, Signal<q31> in, Buffer<q31> out) {
		for (std::size_t i = 0; i < out.size(); ++i) {
			out[i] = in[i];
		}
	}
};

/// A vector processor: a pass-through over Argon<q31>.
struct VecK {
	using value_type = q31;
	using step_type = Argon<q31>;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                              Argon<q31> x) {
		return x;
	}
};

/// A vector processor whose partial last vector advances its count by the active lanes.
struct VecCountedK : VecK {
	static constexpr bool kPartialStepAdvancesByCount = true;
	using VecK::step;
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                              Argon<q31> x, ActiveLanes /*n*/) {
		return x;
	}
};

static_assert(processor_kernel<AddK> && processor_kernel<ScaleK> && generator_kernel<RampGen> && mixer_kernel<SumMix>
              && processor_kernel<QuirkK> && staged_kernel<StagedK> && processor_kernel<VecK>
              && processor_kernel<VecCountedK> && control_kernel<DuckEnvelope>);

using Three = Chain<AddK, ScaleK, Remove>;
using Inner = Chain<AddK, ScaleK>;
using Nested = Chain<Inner, Tanh<3>>;
using Flat = Chain<AddK, ScaleK, Tanh<3>>;
using Generated = Chain<RampGen, Tanh<3>>;
using Mixed = Chain<SumMix, AddK>;
using Quirky = Chain<AddK, QuirkK, ScaleK>;

// Pinned: the kernel at one mode, with no modes of its own.
static_assert(processor_kernel<Remove> && !moded_kernel<Remove> && !detail::has_modes<Remove>);
static_assert(!lane_packable<Remove>, "a pinned kernel steps lane by lane");
static_assert(std::same_as<Remove::Settings, Tracker::Settings> && std::same_as<Remove::State, Tracker::State>
              && std::same_as<Remove::Coefficients, Tracker::Coefficients>
              && std::same_as<kernel_block_t<Remove>, kernel_block_t<Tracker>>);
static_assert(!detail::declares_prepare<Remove> && !detail::declares_reset<Remove>);

// A chain is a kernel of its first part's form.
static_assert(processor_kernel<Three> && generator_kernel<Generated> && mixer_kernel<Mixed>);
static_assert(processor_kernel<Nested> && processor_kernel<Flat>);
static_assert(processor<Mono<Three>> && processor<Stereo<Three>> && generator<Mono<Generated>> && mixer<Mono<Mixed>>);
static_assert(std::same_as<Three::value_type, q31> && std::same_as<Three::step_type, q31>);
static_assert(std::same_as<Three::part_t<0>, AddK> && std::same_as<Three::part_t<1>, ScaleK>
              && std::same_as<Three::part_t<2>, Remove>);

// prepare and reset are declared exactly when a part declares one.
static_assert(detail::declares_prepare<Three> && detail::prepares_exactly<Three>);
static_assert(!detail::declares_prepare<Generated> && !detail::declares_prepare<Mixed>);
static_assert(std::same_as<kernel_block_t<Generated>, EmptyBlock>);
static_assert(!detail::declares_reset<Three> && !detail::declares_reset<Nested>);
static_assert(detail::declares_reset<Quirky> && detail::resets_exactly<Quirky>);
static_assert(detail::declares_reset<Chain<Quirky, Tanh<3>>>, "a nested chain's reset quirk is the outer chain's");

// Every aggregate of a three-part chain is a plain value.
static_assert(detail::plain_value<Three::Settings> && detail::plain_value<Three::Coefficients>
              && detail::plain_value<Three::State> && detail::plain_value<Three::Block>);
static_assert(detail::plain_value<Nested::Settings> && detail::plain_value<Nested::Coefficients>
              && detail::plain_value<Nested::State> && detail::plain_value<Nested::Block>);

// What a chain rejects.
static_assert(chainable<AddK, ScaleK> && chainable<VecK, VecK>);
static_assert(!chainable<AddK>, "a chain has at least two parts");
static_assert(!chainable<AddK, DuckEnvelope> && !chainable<DuckEnvelope, AddK>, "a control kernel is no part");
static_assert(!chainable<AddK, StagedK> && !chainable<StagedK, AddK>, "a staged kernel is no part");
static_assert(!chainable<AddK, SumMix> && !chainable<AddK, RampGen>, "every part after the first is a processor");
static_assert(!chainable<AddK, VecK> && !chainable<VecK, AddK>, "every part steps one step type");
static_assert(!chainable<VecK, VecCountedK>, "a part advancing its partial vector by count is no part");
static_assert(!chainable<AddK, int>, "every part is a kernel");

/// Coefficients shaped as a feedback shaper's: their last byte leaves three bytes of tail padding.
struct ShaperLike {
	q31 feedback{};
	uint32_t drive{};
	uint8_t mode{};
};
/// Coefficients that are a mode alone: one byte, which would fit in a shaper's tail padding.
struct ModeOnly {
	uint8_t mode{};
};

// A part holding bytes is a whole object of its own; only empty parts take no space.
static_assert(sizeof(Parts<ShaperLike, ModeOnly>) >= sizeof(ShaperLike) + sizeof(ModeOnly),
              "no part sits in another's tail padding");
static_assert(sizeof(Parts<q31, EmptyBlock>) == sizeof(q31) && sizeof(Parts<EmptyBlock, q31>) == sizeof(q31)
                  && sizeof(Parts<EmptyBlock, q31, Tanh<3>::State, AddK::Settings>) == 2 * sizeof(q31)
                  && sizeof(Parts<q31, EmptyBlock, Tanh<3>::State>) == sizeof(q31),
              "an empty part takes no space beside parts holding bytes");

/// The block lengths every case runs.
constexpr std::size_t kMaxLength = 9;

/// @p n samples of seeded noise.
std::vector<q31> noise(std::size_t n, uint32_t seed) {
	SeededNoise stream{seed};
	std::vector<q31> v(n);
	for (q31& x : v) {
		x = stream.render();
	}
	return v;
}

/// @p v's bytes, for comparing States bit for bit; an empty State has none.
template <typename T>
std::vector<unsigned char> bytes_of(const T& v) {
	if constexpr (std::is_empty_v<T>) {
		return {};
	}
	else {
		static_assert(std::has_unique_object_representations_v<T>, "a State compared by bytes has no padding");
		const auto bytes = std::bit_cast<std::array<unsigned char, sizeof(T)>>(v);
		return {bytes.begin(), bytes.end()};
	}
}

/// The three-part chain's settings: an offset, a gain and the tracker (whose mode Pinned ignores).
Three::Settings three_settings() {
	return Three::Settings{AddK::Settings{.offset = 0x01234567}, ScaleK::Settings{.base = 0x50000000},
	                       Remove::Settings{.mode = Tracker::Mode::TRACK}};
}

/// The three parts of Three, each its own Mono, run as three passes over a block.
struct Sequential {
	Mono<AddK> add;
	Mono<ScaleK> scale;
	Mono<Remove> remove;

	explicit Sequential(const Three::Settings& s) : add{get<0>(s)}, scale{get<1>(s)}, remove{get<2>(s)} {}

	void renderBlock(const std::vector<q31>& in, std::vector<q31>& out) {
		add.renderBlock(in, out);
		scale.renderBlock(out, out);
		remove.renderBlock(out, out);
	}
};

/// The fused chain's part States' bytes, part 0 first.
std::vector<std::vector<unsigned char>> part_states(const Mono<Three>& fused) {
	const Three::State& s = fused.state();
	return {bytes_of(get<0>(s)), bytes_of(get<1>(s)), bytes_of(get<2>(s))};
}

/// The sequential parts' States' bytes, part 0 first.
std::vector<std::vector<unsigned char>> part_states(const Sequential& parts) {
	return {bytes_of(parts.add.state()), bytes_of(parts.scale.state()), bytes_of(parts.remove.state())};
}

/// @p frames as interleaved samples, left first.
std::vector<q31> interleaved(const std::vector<StereoSample>& frames) {
	std::vector<q31> v;
	for (const StereoSample& f : frames) {
		v.push_back(f.l);
		v.push_back(f.r);
	}
	return v;
}

} // namespace

// clang-format off
describe chain("Chain", ${
	it("equals its parts run as separate passes, bit for bit, at every length", _ {
		for (std::size_t n = 1; n <= kMaxLength; ++n) {
			Mono<Three> fused{three_settings()};
			Sequential parts{three_settings()};
			for (uint32_t block = 0; block < 2; ++block) {
				const std::vector<q31> in = noise(n, 0x5eed + (block * 31) + static_cast<uint32_t>(n));
				std::vector<q31> got(n);
				std::vector<q31> want(n);
				fused.renderBlock(in, got);
				parts.renderBlock(in, want);
				expect(got).to_equal(want);
			}
			expect(part_states(fused)).to_equal(part_states(parts));
		}
	});

	it("equals its parts over a long seeded run of uneven blocks", _ {
		Mono<Three> fused{three_settings()};
		Sequential parts{three_settings()};
		const std::vector<q31> in = noise(4096, 0xc0ffee);
		std::size_t at = 0;
		std::vector<q31> first_got;
		std::vector<q31> first_want;
		for (std::size_t block = 0; at < in.size(); ++block) {
			const std::size_t n = std::min<std::size_t>((block % 61) + 1, in.size() - at);
			const std::vector<q31> slice(in.begin() + static_cast<std::ptrdiff_t>(at),
			                             in.begin() + static_cast<std::ptrdiff_t>(at + n));
			std::vector<q31> got(n);
			std::vector<q31> want(n);
			fused.renderBlock(slice, got);
			parts.renderBlock(slice, want);
			if (got != want && first_got.empty()) {
				first_got = got;
				first_want = want;
			}
			at += n;
		}
		expect(first_got).to_equal(first_want);
		expect(part_states(fused)).to_equal(part_states(parts));
		expect(get<0>(fused.state()).steps).to_equal(4096);
	});

	it("starts from a generator", _ {
		for (std::size_t n = 1; n <= kMaxLength; ++n) {
			const RampGen::Settings ramp{.by = 0x01300000};
			Mono<Generated> fused{Generated::Settings{ramp, Tanh<3>::Settings{}}};
			Mono<RampGen> gen{ramp};
			Mono<Tanh<3>> shape;
			for (int block = 0; block < 2; ++block) {
				std::vector<q31> got(n);
				std::vector<q31> want(n);
				fused.renderBlock(got);
				gen.renderBlock(want);
				shape.renderBlock(want, want);
				expect(got).to_equal(want);
			}
			expect(get<0>(fused.state()).v).to_equal(gen.state().v);
		}
	});

	it("starts from a mixer", _ {
		for (std::size_t n = 1; n <= kMaxLength; ++n) {
			const AddK::Settings add_settings{.offset = -0x00700000};
			Mono<Mixed> fused{Mixed::Settings{SumMix::Settings{}, add_settings}};
			Mono<SumMix> mix;
			Mono<AddK> add{add_settings};
			for (uint32_t block = 0; block < 2; ++block) {
				const std::vector<q31> a = noise(n, 11 + block);
				const std::vector<q31> b = noise(n, 23 + block);
				std::vector<q31> got(n);
				std::vector<q31> want(n);
				fused.renderBlock(a, b, got);
				mix.renderBlock(a, b, want);
				add.renderBlock(want, want);
				expect(got).to_equal(want);
			}
			expect(get<0>(fused.state()).acc).to_equal(mix.state().acc);
			expect(get<1>(fused.state()).steps).to_equal(add.state().steps);
		}
	});

	it("prepares each declaring part once per block, in part order", _ {
		Mono<Chain<PrepLog<1>, AddK, PrepLog<3>, ScaleK>> chain;
		prepare_log.clear();
		std::vector<q31> buffer = noise(5, 99);
		chain.renderBlock(buffer, buffer);
		expect(prepare_log).to_equal(std::vector<int>{1, 3, ScaleK::kLogId});
		chain.renderBlock(buffer, buffer);
		expect(prepare_log).to_equal(std::vector<int>{1, 3, ScaleK::kLogId, 1, 3, ScaleK::kLogId});
		expect(get<3>(chain.state()).prepares).to_equal(2);
		expect(get<1>(chain.state()).steps).to_equal(10);
	});

	it("resets through a part's reset quirk, every other part to State{}", _ {
		Mono<Quirky> chain{Quirky::Settings{AddK::Settings{.offset = 3}, QuirkK::Settings{},
		                                     ScaleK::Settings{.base = 0x40000000}}};
		std::vector<q31> buffer = noise(6, 7);
		chain.renderBlock(buffer, buffer);
		expect(get<0>(chain.state()).steps).to_equal(6);
		expect(get<2>(chain.state()).prepares).to_equal(1);

		chain.reset();
		expect(get<0>(chain.state()).steps).to_equal(0);
		expect(get<1>(chain.state()).z).to_equal(7);
		expect(get<2>(chain.state()).acc).to_equal(0);
		expect(get<2>(chain.state()).prepares).to_equal(0);
	});

	it("resets one part through Mono::on, every other part bit for bit untouched", _ {
		Mono<Three> chain{three_settings()};
		std::vector<q31> buffer = noise(9, 41);
		chain.renderBlock(buffer, buffer);
		chain.renderBlock(buffer, buffer);
		const Three::State before = chain.state();
		expect(get<1>(before).prepares).to_equal(2);

		chain.on<&Three::reset_part<1>>();
		expect(bytes_of(get<0>(chain.state()))).to_equal(bytes_of(get<0>(before)));
		expect(bytes_of(get<1>(chain.state()))).to_equal(bytes_of(ScaleK::State{}));
		expect(bytes_of(get<2>(chain.state()))).to_equal(bytes_of(get<2>(before)));
	});

	it("resets one part through its reset quirk, through Mono::on", _ {
		Mono<Quirky> chain;
		std::vector<q31> buffer = noise(4, 5);
		chain.renderBlock(buffer, buffer);
		const Quirky::State before = chain.state();

		chain.on<&Quirky::reset_part<1>>();
		expect(get<1>(chain.state()).z).to_equal(7);
		expect(bytes_of(get<0>(chain.state()))).to_equal(bytes_of(get<0>(before)));
		expect(bytes_of(get<2>(chain.state()))).to_equal(bytes_of(get<2>(before)));
	});

	it("resets one part in every lane through Stereo::on, every other part untouched", _ {
		Stereo<Three> chain{three_settings()};
		std::vector<StereoSample> frames(7);
		const std::vector<q31> l = noise(frames.size(), 1);
		const std::vector<q31> r = noise(frames.size(), 2);
		for (std::size_t i = 0; i < frames.size(); ++i) {
			frames[i] = {.l = l[i], .r = r[i]};
		}
		chain.renderBlock(frames, frames);
		const Three::State left = chain.left();
		const Three::State right = chain.right();

		chain.on<&Three::reset_part<2>>();
		for (const auto& [now, before] : {std::pair{chain.left(), left}, std::pair{chain.right(), right}}) {
			expect(bytes_of(get<0>(now))).to_equal(bytes_of(get<0>(before)));
			expect(bytes_of(get<1>(now))).to_equal(bytes_of(get<1>(before)));
			expect(get<2>(now).y).to_equal(0);
		}
		expect(get<2>(left).y).not_().to_equal(0);
	});

	it("steps a pinned kernel at its pinned mode, whatever its settings say", _ {
		for (std::size_t n = 1; n <= kMaxLength; ++n) {
			Mono<Remove> pinned{Remove::Settings{.mode = Tracker::Mode::TRACK}};
			Mono<Tracker> removing{Tracker::Settings{.mode = Tracker::Mode::REMOVE}};
			Mono<Tracker> tracking{Tracker::Settings{.mode = Tracker::Mode::TRACK}};
			expect(std::to_underlying(pinned.coefficients().mode)).to_equal(std::to_underlying(Tracker::Mode::REMOVE));
			for (uint32_t block = 0; block < 2; ++block) {
				const std::vector<q31> in = noise(n, 0x4321 + block);
				std::vector<q31> got(n);
				std::vector<q31> removed(n);
				std::vector<q31> tracked(n);
				pinned.renderBlock(in, got);
				removing.renderBlock(in, removed);
				tracking.renderBlock(in, tracked);
				expect(got).to_equal(removed);
				expect(got).not_().to_equal(tracked);
			}
		}
	});

	it("nests: a chain as a part equals the flat chain", _ {
		const Inner::Settings inner{AddK::Settings{.offset = 0x00abcdef}, ScaleK::Settings{.base = 0x60000000}};
		for (std::size_t n = 1; n <= kMaxLength; ++n) {
			Mono<Nested> nested{Nested::Settings{inner, Tanh<3>::Settings{}}};
			Mono<Flat> flat{Flat::Settings{get<0>(inner), get<1>(inner), Tanh<3>::Settings{}}};
			for (uint32_t block = 0; block < 2; ++block) {
				const std::vector<q31> in = noise(n, 0x1234 + block);
				std::vector<q31> got(n);
				std::vector<q31> want(n);
				nested.renderBlock(in, got);
				flat.renderBlock(in, want);
				expect(got).to_equal(want);
			}
			expect(bytes_of(get<1>(get<0>(nested.state())))).to_equal(bytes_of(get<1>(flat.state())));
		}
	});

	it("renders in place exactly as out of place, at every length", _ {
		for (std::size_t n = 1; n <= kMaxLength; ++n) {
			Mono<Three> out_of_place{three_settings()};
			Mono<Three> in_place{three_settings()};
			Stereo<Three> stereo_out_of_place{three_settings()};
			Stereo<Three> stereo_in_place{three_settings()};
			for (uint32_t block = 0; block < 2; ++block) {
				const std::vector<q31> in = noise(n, 0x777 + block);
				std::vector<q31> got(n);
				out_of_place.renderBlock(in, got);
				std::vector<q31> buffer = in;
				in_place.renderBlock(buffer, buffer);
				expect(buffer).to_equal(got);

				const std::vector<q31> r = noise(n, 0x999 + block);
				std::vector<StereoSample> frames(n);
				for (std::size_t i = 0; i < n; ++i) {
					frames[i] = {.l = in[i], .r = r[i]};
				}
				std::vector<StereoSample> stereo_got(n);
				stereo_out_of_place.renderBlock(frames, stereo_got);
				stereo_in_place.renderBlock(frames, frames);
				expect(interleaved(frames)).to_equal(interleaved(stereo_got));
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(chain);
