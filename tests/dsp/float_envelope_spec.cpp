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
// fp::Envelope: its programs on Mono, the lanes of a Poly in different stages at once against their Monos, and its
// output pinned by digest (float_kernel_records.h).

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/poly.h"
#include "dsp-next/float/envelope/envelope.h"

#include "float_kernel_records.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <set>
#include <span>
#include <vector>

using namespace deluge::dsp;

// The spec's own float arithmetic rounds as written, as the kernels' does, so its twin built with the device's float
// flags (float_envelope_unsafe_math_spec) holds the kernels to the same records. The headers above pin themselves.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

static_assert(poly_kernel<fp::Envelope>);

namespace {

using Env = Mono<fp::Envelope>;

/// FNV-1a over the output's bits: the digest a record pins.
[[nodiscard]] uint32_t digest(std::span<const f32> out) {
	uint32_t h = 2166136261U;
	for (f32 x : out) {
		h = (h ^ std::bit_cast<uint32_t>(x)) * 16777619U;
	}
	return h;
}

/// The next @p n samples of @p env, rendered one at a time.
[[nodiscard]] std::vector<f32> run(Env& env, std::size_t n) {
	std::vector<f32> out(n);
	for (f32& y : out) {
		y = env.render();
	}
	return out;
}

/// The index of the first sample of @p v equal to @p x, or v.size() when none is.
[[nodiscard]] std::size_t first_equal(const std::vector<f32>& v, f32 x) {
	return static_cast<std::size_t>(std::ranges::find(v, x) - v.begin());
}

[[nodiscard]] Env make(const fp::Program& program) {
	Env env;
	env.configure({.program = program});
	return env;
}

[[nodiscard]] uint32_t bits(f32 x) {
	return std::bit_cast<uint32_t>(x);
}

constexpr std::size_t kBlock = 37;
constexpr std::size_t kBlocks = 12;

/// One lane's program of the divergence scenario, by lane: chosen so the stages end on different samples.
[[nodiscard]] fp::Program divergence_program(std::size_t lane) {
	switch (lane) {
	case 0:
		return fp::adsr(5.f, 8.f, 0.5f, 10.f);
	case 1:
		return fp::adsr(9.f, 20.f, 0.6f, 12.f);
	case 2:
		return fp::ad(6.f, 15.f);
	default:
		return fp::four_segment({0.8f, 0.3f, 0.6f, 0.1f}, {23.f, 31.f, 17.f, 29.f});
	}
}

/// The events lane @p lane gets before block @p block: 1 is a gate-on, -1 a gate-off, 0 none.
[[nodiscard]] int divergence_event(std::size_t lane, std::size_t block) {
	switch (lane) {
	case 0:
		return block == 0 ? 1 : (block == 5 ? -1 : 0);
	case 1:
		// Gated on at block 1; off at block 3 (mid-decay); on again at block 6 (mid-release); off at block 10.
		return block == 1 ? 1 : (block == 3 ? -1 : (block == 6 ? 1 : (block == 10 ? -1 : 0)));
	case 2:
		return block == 2 ? 1 : (block == 4 ? -1 : 0); // AD: the gate-off is ignored
	default:
		return block == 3 ? 1 : (block == 8 ? -1 : 0);
	}
}

/// N lanes of different programs, gated at different blocks, against N Monos: every output sample and final state
/// equal bit for bit, with the scenario's stages recorded from the Monos to show the lanes diverge.
template <std::size_t N>
void expect_divergent_lanes_equal_mono(auto& self) {
	fp::FlushToZero ftz;
	Poly<fp::Envelope, N> poly;
	std::array<Env, N> mono{};
	for (std::size_t l = 0; l < N; ++l) {
		poly.configure(l, {.program = divergence_program(l)});
		mono[l].configure({.program = divergence_program(l)});
	}
	std::array<std::vector<f32>, N> out{}, ref{};
	std::set<std::size_t> transition_offsets;
	std::set<int> stages_at_once;
	std::array<std::set<int>, N> stages_visited{};
	bool retriggered_in_release = false;
	for (std::size_t b = 0; b < kBlocks; ++b) {
		for (std::size_t l = 0; l < N; ++l) {
			const int e = divergence_event(l, b);
			if (e > 0) {
				retriggered_in_release = retriggered_in_release || (b > 0 && mono[l].state().index == 3.f);
				poly.template on<&fp::Envelope::gate_on>(l);
				mono[l].template on<&fp::Envelope::gate_on>();
			}
			else if (e < 0) {
				poly.template on<&fp::Envelope::gate_off>(l);
				mono[l].template on<&fp::Envelope::gate_off>();
			}
		}
		std::array<std::vector<f32>, N> block{};
		std::array<fp::Buffer, N> spans{};
		for (std::size_t l = 0; l < N; ++l) {
			block[l].resize(kBlock);
			spans[l] = fp::Buffer{block[l]};
		}
		poly.render_block(spans);
		for (std::size_t l = 0; l < N; ++l) {
			out[l].insert(out[l].end(), block[l].begin(), block[l].end());
		}
		for (std::size_t i = 0; i < kBlock; ++i) {
			std::array<int, N> stage{};
			for (std::size_t l = 0; l < N; ++l) {
				const f32 before = mono[l].state().index;
				ref[l].push_back(mono[l].render());
				const f32 after = mono[l].state().index;
				if (after != before) {
					transition_offsets.insert(i);
				}
				stage[l] = static_cast<int>(after);
				stages_visited[l].insert(stage[l]);
			}
			// Distinct stages across the lanes that are running (not at rest) at this sample.
			std::set<int> running;
			for (const int st : stage) {
				if (st >= 0) {
					running.insert(st);
				}
			}
			if (running.size() >= std::min<std::size_t>(N, 3)) {
				stages_at_once.insert(static_cast<int>(running.size()));
			}
		}
	}
	for (std::size_t l = 0; l < N; ++l) {
		for (std::size_t i = 0; i < out[l].size(); ++i) {
			expect(bits(out[l][i])).to_equal(bits(ref[l][i]));
		}
		const auto ps = poly.state(l);
		const auto ms = mono[l].state();
		expect(std::bit_cast<std::array<uint32_t, sizeof(ps) / 4>>(ps)
		       == std::bit_cast<std::array<uint32_t, sizeof(ms) / 4>>(ms))
		    .to_be_true();
	}
	// The scenario really diverges: transitions fall on several offsets within a block, lanes sit in different
	// stages together, a lane retriggers during its release, and a lane that was gated on reaches rest.
	expect(transition_offsets.size() >= 4).to_be_true();
	expect(stages_at_once.empty()).to_be_false();
	expect(retriggered_in_release).to_be_true();
	expect(mono[0].state().index).to_equal(-1.f);
	if constexpr (N >= 2) {
		expect(stages_visited[0].contains(0)).to_be_true();
		expect(stages_visited[0].contains(1)).to_be_true();
		expect(stages_visited[0].contains(2)).to_be_true();
		expect(stages_visited[0].contains(3)).to_be_true();
		expect(stages_visited[0].contains(-1)).to_be_true();
	}
	if constexpr (N == 4) {
		expect(mono[2].state().index).to_equal(-1.f);
		expect(stages_visited[3].contains(2)).to_be_true();
		expect(stages_visited[3].contains(3)).to_be_true();
	}
}

} // namespace

// clang-format off
describe float_envelope_spec("float envelope", ${
	fp::FlushToZero ftz;
	context("ADSR", _ {
		const f32 attack = 100.f, decay = 200.f, sustain = 0.5f, release = 300.f;
		it("rises to exactly 1, decays toward the sustain, holds it, releases to exactly 0 and rests", _ {
			Env env = make(fp::adsr(attack, decay, sustain, release));
			env.on<&fp::Envelope::gate_on>();
			const auto up = run(env, 1000);
			const std::size_t top = first_equal(up, 1.f);
			expect(top < up.size()).to_be_true();
			expect(std::ranges::all_of(std::span{up}.first(top), [](f32 y) { return y < 1.f && y >= 0.f; })).to_be_true();
			// The sample k after the landing has had k decay steps: sustain + (1 - sustain) e^(-k/decay).
			expect(std::fabs(up[top + 200] - (0.5f + (0.5f * std::exp(-1.f)))) < 1e-3f).to_be_true();
			expect(run(env, 4000).back()).to_equal(sustain); // landed on the sustain exactly, and held it
			env.on<&fp::Envelope::gate_off>();
			const auto rel = run(env, 5000);
			expect(std::fabs(rel[299] - (0.5f * std::exp(-1.f))) < 1e-3f).to_be_true();
			const std::size_t gone = first_equal(rel, 0.f);
			expect(gone < rel.size()).to_be_true();
			expect(std::ranges::all_of(std::span{rel}.subspan(gone), [](f32 y) { return y == 0.f; })).to_be_true();
			expect(env.state().index).to_equal(-1.f);
		});
		it("starts a retrigger from the current value", _ {
			Env env = make(fp::adsr(attack, decay, sustain, release));
			env.on<&fp::Envelope::gate_on>();
			static_cast<void>(run(env, 1500));
			env.on<&fp::Envelope::gate_off>();
			const auto rel = run(env, 100);
			env.on<&fp::Envelope::gate_on>();
			const f32 next = env.render();
			const f32 step = (1.f - std::exp(-1.f / attack)) * 1.2f;
			expect(next > rel.back()).to_be_true();
			expect(next - rel.back() < step).to_be_true();
		});
	});
	context("AD and ASR", _ {
		it("ignores a gate-off in an AD", _ {
			Env with = make(fp::ad(20.f, 40.f)), without = make(fp::ad(20.f, 40.f));
			with.on<&fp::Envelope::gate_on>();
			without.on<&fp::Envelope::gate_on>();
			auto a = run(with, 30);
			auto b = run(without, 30);
			with.on<&fp::Envelope::gate_off>();
			const auto a2 = run(with, 1000);
			const auto b2 = run(without, 1000);
			a.insert(a.end(), a2.begin(), a2.end());
			b.insert(b.end(), b2.begin(), b2.end());
			for (std::size_t i = 0; i < a.size(); ++i) {
				expect(bits(a[i])).to_equal(bits(b[i]));
			}
			expect(a.back()).to_equal(0.f);
			expect(*std::ranges::max_element(a)).to_equal(1.f);
		});
		it("holds an ASR at 1 until the gate-off", _ {
			Env env = make(fp::asr(20.f, 50.f));
			env.on<&fp::Envelope::gate_on>();
			const auto up = run(env, 800);
			const std::size_t top = first_equal(up, 1.f);
			expect(top < up.size()).to_be_true();
			expect(std::ranges::all_of(std::span{up}.subspan(top), [](f32 y) { return y == 1.f; })).to_be_true();
			env.on<&fp::Envelope::gate_off>();
			const auto rel = run(env, 1000);
			expect(rel.front() < 1.f).to_be_true();
			expect(rel.back()).to_equal(0.f);
		});
	});
	context("four segments", _ {
		it("visits L1 and L2, holds L3, releases to L4 and stays there", _ {
			Env env = make(fp::four_segment({0.8f, 0.3f, 0.6f, 0.1f}, {50.f, 40.f, 30.f, 60.f}));
			env.on<&fp::Envelope::gate_on>();
			const auto up = run(env, 400);
			const std::size_t l1 = first_equal(up, 0.8f);
			const std::size_t l2 = first_equal(up, 0.3f);
			const std::size_t l3 = first_equal(up, 0.6f);
			expect(l1 < l2).to_be_true();
			expect(l2 < l3).to_be_true();
			expect(l3 < up.size()).to_be_true();
			expect(std::ranges::all_of(std::span{up}.subspan(l3), [](f32 y) { return y == 0.6f; })).to_be_true();
			env.on<&fp::Envelope::gate_off>();
			const auto rel = run(env, 400);
			const std::size_t l4 = first_equal(rel, 0.1f);
			expect(l4 < rel.size()).to_be_true();
			expect(std::ranges::all_of(std::span{rel}.subspan(l4), [](f32 y) { return y == 0.1f; })).to_be_true();
		});
	});
	context("looping", _ {
		it("repeats its two segments", _ {
			Env env = make(fp::looping(0.2f, 0.9f, 40.f));
			env.on<&fp::Envelope::gate_on>();
			const auto out = run(env, 40 * 2 * 5);
			expect(std::ranges::count(out, 0.9f) >= 3).to_be_true();
			expect(std::ranges::count(out, 0.2f) >= 3).to_be_true();
			expect(std::ranges::all_of(out, [](f32 y) { return y >= 0.f && y <= 0.9f; })).to_be_true();
		});
	});
	context("lanes", _ {
		it("diverge across two lanes, each bit for bit its Mono", _ { expect_divergent_lanes_equal_mono<2>(self); });
		it("diverge across four lanes, each bit for bit its Mono", _ { expect_divergent_lanes_equal_mono<4>(self); });
	});
	context("records", _ {
		it("matches its records", _ {
			Env adsr = make(fp::adsr(30.f, 60.f, 0.5f, 80.f));
			Env four = make(fp::four_segment({0.8f, 0.3f, 0.6f, 0.1f}, {50.f, 40.f, 30.f, 60.f}));
			adsr.on<&fp::Envelope::gate_on>();
			four.on<&fp::Envelope::gate_on>();
			auto a = run(adsr, 500);
			auto f = run(four, 300);
			adsr.on<&fp::Envelope::gate_off>();
			four.on<&fp::Envelope::gate_off>();
			const auto a2 = run(adsr, 500);
			const auto f2 = run(four, 300);
			a.insert(a.end(), a2.begin(), a2.end());
			f.insert(f.end(), f2.begin(), f2.end());
			expect(digest(a)).to_equal(records::kEnvAdsr);
			expect(digest(f)).to_equal(records::kEnvFourSegment);
		});
	});
});
// clang-format on
CPPSPEC_MAIN(float_envelope_spec);
