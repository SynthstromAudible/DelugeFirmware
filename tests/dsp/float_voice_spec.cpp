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
// The float voice: Chain<oscillator, Svf, Vca<Envelope>> as one kernel. On Mono it equals its parts run in sequence;
// under Poly each lane, with its own pitch, cutoff, program and gate times, equals its own Mono chain; set_lane and
// get_lane move a lane of the chain's coefficients and state; its output is pinned by digest (float_kernel_records.h).

#include "dsp-next/core/chain.h"
#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/poly.h"
#include "dsp-next/float/envelope/envelope.h"
#include "dsp-next/float/filter/smoother.h"
#include "dsp-next/float/filter/svf.h"
#include "dsp-next/float/osc/blep_oscillator.h"
#include "dsp-next/float/osc/saw_bands.h"
#include "dsp-next/float/osc/table_oscillator.h"
#include "dsp-next/float/vca/vca.h"

#include "float_kernel_records.h"

#include <array>
#include <bit>
#include <cppspec.hpp>
#include <cstdint>
#include <random>
#include <span>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;

// The spec's own float arithmetic rounds as written, as the kernels' does, so its twins built with the device's float
// flags and with -mfma hold the kernels to the same records. The headers pin themselves.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

namespace {

using VcaEnv = fp::Vca<fp::Envelope>;
using BlepVoice = Chain<fp::BlepOscillator, fp::Svf, VcaEnv>;
using TableVoice = Chain<fp::TableOscillator, fp::Svf, VcaEnv>;
/// A chain whose first part declares a `prepare`: its packed prepare runs once per block, as Mono's does.
using Smoothed = Chain<fp::Smoother, fp::Svf>;

static_assert(poly_kernel<BlepVoice> && poly_kernel<TableVoice> && poly_kernel<Smoothed>);
static_assert(generator_kernel<BlepVoice> && processor_kernel<Smoothed>);
static_assert(std::same_as<BlepVoice::Coefficients, BlepVoice::CoefficientsT<f32>>
              && std::same_as<BlepVoice::State, BlepVoice::StateT<f32>>);
static_assert(detail::declares_prepare<Smoothed> && !detail::declares_prepare<BlepVoice>);

constexpr std::size_t kBlock = 37;
constexpr std::size_t kBlocks = 12;

[[nodiscard]] uint32_t bits(f32 x) {
	return std::bit_cast<uint32_t>(x);
}

/// FNV-1a over the output's bits: the digest a record pins.
[[nodiscard]] uint32_t digest(std::span<const f32> out) {
	uint32_t h = 2166136261U;
	for (f32 x : out) {
		h = (h ^ bits(x)) * 16777619U;
	}
	return h;
}

[[nodiscard]] std::vector<f32> noise(std::size_t n, uint32_t seed) {
	std::mt19937 rng{seed};
	std::uniform_real_distribution<f32> d{-1.f, 1.f};
	std::vector<f32> v(n);
	for (f32& x : v) {
		x = d(rng);
	}
	return v;
}

template <class T, std::size_t K>
[[nodiscard]] bool same_fields(const std::array<T, K>& a, const std::array<T, K>& b);

/// Whether every field of @p a and @p b, through arrays and nested field structs, has the same bits. Members that are
/// not fields (the modes) are not compared.
template <class T>
[[nodiscard]] bool same_fields(const T& a, const T& b) {
	if constexpr (std::same_as<T, f32>) {
		return bits(a) == bits(b);
	}
	else {
		return std::apply([&](auto... m) { return (same_fields(a.*m, b.*m) && ...); }, T::fields);
	}
}

template <class T, std::size_t K>
[[nodiscard]] bool same_fields(const std::array<T, K>& a, const std::array<T, K>& b) {
	for (std::size_t k = 0; k < K; ++k) {
		if (!same_fields(a[k], b[k])) {
			return false;
		}
	}
	return true;
}

/// Lane @p lane's envelope program: a different shape and times per lane.
[[nodiscard]] fp::Program program(std::size_t lane) {
	const auto t = static_cast<f32>(lane);
	switch (lane % 3) {
	case 0:
		return fp::adsr(20.f + (7.f * t), 40.f, 0.5f, 60.f + (11.f * t));
	case 1:
		return fp::four_segment({0.9f, 0.4f, 0.6f, 0.1f}, {30.f, 25.f + t, 40.f, 50.f});
	default:
		return fp::asr(15.f + t, 90.f);
	}
}

/// Lane @p lane's gate in block @p b: +1 on, -1 off, 0 none. Each lane is gated on and off at its own blocks, and
/// lane 1 retriggers.
[[nodiscard]] int gate(std::size_t lane, std::size_t b) {
	if (b == lane) {
		return 1;
	}
	if (b == lane + 4 || (lane == 1 && b == 10)) {
		return -1;
	}
	if (lane == 1 && b == 7) {
		return 1;
	}
	return 0;
}

[[nodiscard]] VcaEnv::Settings vca_settings(std::size_t lane) {
	return {.envelope = {.program = program(lane)}, .gain = 0.3f + (0.2f * static_cast<f32>(lane))};
}

[[nodiscard]] fp::Svf::Settings svf_settings(std::size_t lane, fp::Svf::Mode mode) {
	return {.cutoff = 0.03f + (0.05f * static_cast<f32>(lane)), .q = 0.8f + static_cast<f32>(lane), .mode = mode};
}

[[nodiscard]] f32 frequency(std::size_t lane) {
	return 0.0041f + (0.0137f * static_cast<f32>(lane));
}

[[nodiscard]] BlepVoice::Settings blep_settings(std::size_t lane, fp::BlepOscillator::Mode osc, fp::Svf::Mode svf) {
	const fp::BlepOscillator::Settings o{
	    .frequency = frequency(lane), .width = 0.3f + (0.1f * static_cast<f32>(lane)), .mode = osc};
	return BlepVoice::Settings{o, svf_settings(lane, svf), vca_settings(lane)};
}

[[nodiscard]] TableVoice::Settings table_settings(std::size_t lane, fp::Svf::Mode svf) {
	const fp::TableOscillator::Settings o{.frequency = frequency(lane)};
	return TableVoice::Settings{o, svf_settings(lane, svf), vca_settings(lane)};
}

/// A voice on Mono, rendered block by block with lane 0's gates, against its oscillator, Svf and Vca on Monos of
/// their own, each block run through them in sequence: every sample and the final state of each part equal bit for
/// bit.
template <class Voice, class Osc>
void expect_voice_equals_parts(auto& self, const typename Voice::Settings& settings) {
	fp::FlushToZero ftz;
	Mono<Voice> voice;
	Mono<Osc> osc;
	Mono<fp::Svf> svf;
	Mono<VcaEnv> vca;
	voice.configure(settings);
	osc.configure(get<0>(settings));
	svf.configure(get<1>(settings));
	vca.configure(get<2>(settings));
	bool audible = false;
	for (std::size_t b = 0; b < kBlocks; ++b) {
		const int g = gate(0, b);
		if (g > 0) {
			voice.template on<part_event<2, &VcaEnv::gate_on>>();
			vca.template on<&VcaEnv::gate_on>();
		}
		else if (g < 0) {
			voice.template on<part_event<2, &VcaEnv::gate_off>>();
			vca.template on<&VcaEnv::gate_off>();
		}
		std::vector<f32> out(kBlock), wave(kBlock), filtered(kBlock), ref(kBlock);
		voice.render_block(fp::Buffer{out});
		osc.render_block(fp::Buffer{wave});
		svf.render_block(fp::Signal{wave}, fp::Buffer{filtered});
		vca.render_block(fp::Signal{filtered}, fp::Buffer{ref});
		for (std::size_t i = 0; i < kBlock; ++i) {
			expect(bits(out[i])).to_equal(bits(ref[i]));
			audible = audible || ref[i] != 0.f;
		}
	}
	expect(same_fields(get<0>(voice.state()), osc.state())).to_be_true();
	expect(same_fields(get<1>(voice.state()), svf.state())).to_be_true();
	expect(same_fields(get<2>(voice.state()), vca.state())).to_be_true();
	expect(audible).to_be_true();
}

/// N lanes of a voice under Poly, each with its own settings (one mode for all) and gated at its own blocks through
/// part_event, against N Mono voices: every sample of 12 blocks of 37, and each lane's final state, equal bit for bit.
template <class Voice, std::size_t N>
void expect_voice_lanes_equal_mono(auto& self, auto settings) {
	fp::FlushToZero ftz;
	Poly<Voice, N> poly;
	std::array<Mono<Voice>, N> mono{};
	for (std::size_t l = 0; l < N; ++l) {
		poly.configure(l, settings(l));
		mono[l].configure(settings(l));
	}
	bool audible = false;
	for (std::size_t b = 0; b < kBlocks; ++b) {
		for (std::size_t l = 0; l < N; ++l) {
			const int g = gate(l, b);
			if (g > 0) {
				poly.template on<part_event<2, &VcaEnv::gate_on>>(l);
				mono[l].template on<part_event<2, &VcaEnv::gate_on>>();
			}
			else if (g < 0) {
				poly.template on<part_event<2, &VcaEnv::gate_off>>(l);
				mono[l].template on<part_event<2, &VcaEnv::gate_off>>();
			}
		}
		std::array<std::vector<f32>, N> out{}, ref{};
		std::array<fp::Buffer, N> spans{};
		for (std::size_t l = 0; l < N; ++l) {
			out[l].resize(kBlock);
			ref[l].resize(kBlock);
			spans[l] = fp::Buffer{out[l]};
			mono[l].render_block(fp::Buffer{ref[l]});
		}
		poly.render_block(spans);
		for (std::size_t l = 0; l < N; ++l) {
			for (std::size_t i = 0; i < kBlock; ++i) {
				expect(bits(out[l][i])).to_equal(bits(ref[l][i]));
				audible = audible || ref[l][i] != 0.f;
			}
		}
	}
	for (std::size_t l = 0; l < N; ++l) {
		expect(same_fields(poly.state(l), mono[l].state())).to_be_true();
	}
	expect(audible).to_be_true();
}

/// N lanes of a Smoother into an Svf under Poly, each with its own target set again every block, against N Monos:
/// the chain's prepare runs on the packed lanes once per block as each Mono's does on one.
template <std::size_t N>
void expect_prepared_lanes_equal_mono(auto& self) {
	fp::FlushToZero ftz;
	Poly<Smoothed, N> poly;
	std::array<Mono<Smoothed>, N> mono{};
	const auto in = noise(kBlock, 3);
	for (std::size_t b = 0; b < kBlocks; ++b) {
		for (std::size_t l = 0; l < N; ++l) {
			const fp::Smoother::Settings smoother{.target = static_cast<f32>((b * 7 + l * 3) % 5) - 2.f,
			                                      .mode = fp::Smoother::Mode::LINEAR};
			const Smoothed::Settings settings{smoother, svf_settings(l, fp::Svf::Mode::LOW_PASS)};
			poly.configure(l, settings);
			mono[l].configure(settings);
		}
		std::array<std::vector<f32>, N> out{}, ref{};
		std::array<fp::Signal, N> signal{};
		std::array<fp::Buffer, N> spans{};
		for (std::size_t l = 0; l < N; ++l) {
			out[l].resize(kBlock);
			ref[l].resize(kBlock);
			signal[l] = fp::Signal{in};
			spans[l] = fp::Buffer{out[l]};
			mono[l].render_block(signal[l], fp::Buffer{ref[l]});
		}
		poly.render_block(signal, spans);
		for (std::size_t l = 0; l < N; ++l) {
			for (std::size_t i = 0; i < kBlock; ++i) {
				expect(bits(out[l][i])).to_equal(bits(ref[l][i]));
			}
		}
	}
	for (std::size_t l = 0; l < N; ++l) {
		expect(same_fields(poly.state(l), mono[l].state())).to_be_true();
	}
}

/// A voice's record: gated on for 600 samples, then released for 600, on lane 0's settings.
template <class Voice>
[[nodiscard]] uint32_t record(const typename Voice::Settings& settings) {
	fp::FlushToZero ftz;
	Mono<Voice> voice;
	voice.configure(settings);
	voice.template on<part_event<2, &VcaEnv::gate_on>>();
	std::vector<f32> out(1200);
	voice.render_block(fp::Buffer{out}.subspan(0, 600));
	voice.template on<part_event<2, &VcaEnv::gate_off>>();
	voice.render_block(fp::Buffer{out}.subspan(600));
	return digest(out);
}

using fp::BlepOscillator;
using fp::Svf;

auto blep(BlepOscillator::Mode osc, Svf::Mode svf) {
	return [=](std::size_t l) { return blep_settings(l, osc, svf); };
}
auto table(Svf::Mode svf) {
	return [=](std::size_t l) { return table_settings(l, svf); };
}

} // namespace

// clang-format off
describe float_voice_spec("float voice", ${
	fp::build_saw_bands();

	context("on Mono", _ {
		it("equals its BlepOscillator, Svf and Vca run in sequence, saw into a low-pass", _ {
			expect_voice_equals_parts<BlepVoice, BlepOscillator>(
			    self, blep_settings(0, BlepOscillator::Mode::SAW, Svf::Mode::LOW_PASS));
		});
		it("equals its BlepOscillator, Svf and Vca run in sequence, pulse into a band-pass", _ {
			expect_voice_equals_parts<BlepVoice, BlepOscillator>(
			    self, blep_settings(0, BlepOscillator::Mode::PULSE, Svf::Mode::BAND_PASS));
		});
		it("equals its TableOscillator, Svf and Vca run in sequence", _ {
			expect_voice_equals_parts<TableVoice, fp::TableOscillator>(self, table_settings(0, Svf::Mode::HIGH_PASS));
		});
	});

	context("under Poly", _ {
		it("runs two Blep voices, each lane bit for bit its Mono", _ {
			expect_voice_lanes_equal_mono<BlepVoice, 2>(self, blep(BlepOscillator::Mode::SAW, Svf::Mode::LOW_PASS));
		});
		it("runs four Blep voices, each lane bit for bit its Mono", _ {
			expect_voice_lanes_equal_mono<BlepVoice, 4>(self, blep(BlepOscillator::Mode::SAW, Svf::Mode::LOW_PASS));
		});
		it("runs four Blep voices at the chain's last mode, each lane bit for bit its Mono", _ {
			expect_voice_lanes_equal_mono<BlepVoice, 4>(self, blep(BlepOscillator::Mode::PULSE, Svf::Mode::NOTCH));
		});
		it("runs two Table voices, each lane bit for bit its Mono", _ {
			expect_voice_lanes_equal_mono<TableVoice, 2>(self, table(Svf::Mode::LOW_PASS));
		});
		it("runs four Table voices, each lane bit for bit its Mono", _ {
			expect_voice_lanes_equal_mono<TableVoice, 4>(self, table(Svf::Mode::BAND_PASS));
		});
		it("prepares a chain's packed lanes once per block, each lane bit for bit its Mono", _ {
			expect_prepared_lanes_equal_mono<2>(self);
			expect_prepared_lanes_equal_mono<4>(self);
		});
	});

	context("lanes", _ {
		it("moves a lane of a voice's coefficients and state, the envelope's arrays included", _ {
			fp::FlushToZero ftz;
			BlepVoice::CoefficientsT<Pack<f32, 4>> packed_c{};
			BlepVoice::StateT<Pack<f32, 4>> packed_s{};
			std::array<BlepVoice::Coefficients, 4> c{};
			std::array<BlepVoice::State, 4> s{};
			for (std::size_t l = 0; l < 4; ++l) {
				c[l] = BlepVoice::coefficients(blep_settings(l, BlepOscillator::Mode::SAW, Svf::Mode::LOW_PASS));
				Mono<BlepVoice> voice;
				voice.configure(blep_settings(l, BlepOscillator::Mode::SAW, Svf::Mode::LOW_PASS));
				voice.on<part_event<2, &VcaEnv::gate_on>>();
				std::vector<f32> out(20 + (13 * l));
				voice.render_block(fp::Buffer{out});
				s[l] = voice.state();
				set_lane(packed_c, l, c[l]);
				set_lane(packed_s, l, s[l]);
			}
			for (std::size_t l = 0; l < 4; ++l) {
				expect(same_fields(get_lane<BlepVoice::Coefficients>(packed_c, l), c[l])).to_be_true();
				expect(same_fields(get_lane<BlepVoice::State>(packed_s, l), s[l])).to_be_true();
			}
			// Each lane's program differs, so a lane read from the wrong place could not match.
			expect(bits(get<2>(c[0]).envelope.segments[0].rate) != bits(get<2>(c[2]).envelope.segments[0].rate))
			    .to_be_true();
		});
	});

	context("records", _ {
		it("matches its records", _ {
			expect(record<BlepVoice>(blep_settings(0, BlepOscillator::Mode::SAW, Svf::Mode::LOW_PASS)))
			    .to_equal(records::kBlepVoice);
			expect(record<TableVoice>(table_settings(0, Svf::Mode::LOW_PASS))).to_equal(records::kTableVoice);
		});
	});
});
// clang-format on

CPPSPEC_MAIN(float_voice_spec);
