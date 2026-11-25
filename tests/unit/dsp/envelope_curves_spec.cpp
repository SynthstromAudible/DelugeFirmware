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

// Envelope against main's curves sampled at their exact times: each mode, each curve, a rate modulated every
// block, a retrigger mid-release, the fast release and a held level of 0. Within 2^-16 of full scale, every block.

#include "synth/modulation/envelope.h"

#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>
#include <vector>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

using deluge::dsp::Q;
using deluge::synth::EnvelopeFollow;
using deluge::synth::EnvelopeMode;
using deluge::synth::EnvelopeState;
using Stage = Envelope::Stage;

namespace {

constexpr uint32_t kBlock = 32;
constexpr double kStageEnd = 8388608.0;
constexpr double kTolerance = 0x1p-16;

[[nodiscard]] constexpr uint32_t rate_for(uint32_t samples) {
	return 8388608U / samples;
}
[[nodiscard]] double length(uint32_t rate) {
	return kStageEnd / rate;
}
/// main's falling curve in double.
[[nodiscard]] double falling(double k, double x) {
	x = std::clamp(x, 0.0, 1.0);
	return k == 0.0 ? 1.0 - x : (std::exp2(-k * x) - std::exp2(-k)) / (1.0 - std::exp2(-k));
}
/// An output word's level: the centred word back to 0..1.
[[nodiscard]] double level_of(Q<31> word) {
	return ((static_cast<double>(word.raw()) / 2147483648.0) + 1.0) / 2.0;
}
[[nodiscard]] Q<31> q31(double level) {
	return Q<31>::from_raw(static_cast<int32_t>(std::min(level * 2147483648.0, 2147483647.0)));
}
[[nodiscard]] EnvelopeState state(EnvelopeMode mode, int32_t curve, EnvelopeFollow follow = EnvelopeFollow::Z) {
	return {.mode = mode, .follow = follow, .curve = EnvelopeState::clamped_curve(curve)};
}

constexpr uint32_t kAttack = rate_for(600);
constexpr uint32_t kDecay = rate_for(900);
constexpr uint32_t kRelease = rate_for(1200);

} // namespace

// clang-format off
describe envelope_curves_spec_body("Envelope", ${
	it("runs ADSR along main's curves at every curve control", _ {
		for (int32_t curve : {-50, -20, 0, 1, 16, 32, 50}) {
			const EnvelopeState s = state(EnvelopeMode::ADSR, curve);
			const double ka = curve * 0.125, kd = curve * 0.25;
			const double la = length(kAttack), ld = length(kDecay), lr = length(kRelease);
			const double sustain = 0.5;
			Envelope env;
			(void)env.note_on(false, q31(sustain), s);
			double at_off = 0.0;
			for (uint32_t b = 0; b < 128; ++b) {
				const double t = static_cast<double>((b + 1) * kBlock);
				if (b == 80) {
					env.note_off(true);
				}
				const double y = level_of(env.render(kBlock, kAttack, kDecay, q31(sustain), kRelease, s, Q<31>{}));
				double expected = 0.0;
				if (b < 80) {
					expected = t < la ? 1.0 - falling(ka, t / la)
					           : t < la + ld ? sustain + ((1.0 - sustain) * falling(kd, (t - la) / ld))
					                         : sustain;
					at_off = expected;
				}
				else {
					const double tr = t - (80.0 * kBlock);
					expected = tr < lr ? at_off * falling(kd, tr / lr) : 0.0;
				}
				expect(y).to_be_within(kTolerance).of(expected);
			}
			expect(env.stage()).to_equal(Stage::OFF);
		}
	});

	it("runs AHD's hold and decay from its decay and release params, ignoring the note-off", _ {
		const EnvelopeState s = state(EnvelopeMode::AHD, 32);
		const uint32_t hold_param = rate_for(4000); // the hold is an eighth: 500 samples
		const double la = length(kAttack), lh = length(hold_param * 8), ld = length(kRelease);
		Envelope env;
		(void)env.note_on(false, q31(0.5), s);
		bool held = false;
		for (uint32_t b = 0; b < 128; ++b) {
			const double t = static_cast<double>((b + 1) * kBlock);
			if (b == 10) {
				env.note_off(true);
			}
			const double y = level_of(env.render(kBlock, kAttack, hold_param, q31(0.5), kRelease, s, Q<31>{}));
			const double expected = t < la ? 1.0 - falling(4.0, t / la)
			                        : t < la + lh ? 1.0
			                        : t < la + lh + ld ? falling(8.0, (t - la - lh) / ld)
			                                           : 0.0;
			expect(y).to_be_within(kTolerance).of(expected);
			held = held || env.stage() == Stage::HOLD;
		}
		expect(held).to_equal(true);
		expect(env.stage()).to_equal(Stage::OFF);
	});

	it("slews AR to its level both ways at the attack and release rates", _ {
		for (int32_t curve : {0, 32}) {
			const EnvelopeState s = state(EnvelopeMode::AR, curve);
			const double k = curve * 0.25;
			Envelope env;
			(void)env.note_on(false, q31(0.7), s);
			double model = 0.0;
			for (uint32_t b = 0; b < 100; ++b) {
				const double target = b < 50 ? 0.7 : 0.2;
				const uint32_t rate = target > model ? kAttack : kRelease;
				const double distance = static_cast<double>(rate) * kBlock / kStageEnd;
				model = k > 0.0 ? target + ((model - target) * std::exp2(-k * distance))
				                : model + std::clamp(target - model, -distance, distance);
				const double y = level_of(env.render(kBlock, kAttack, kDecay, q31(target), kRelease, s, Q<31>{}));
				expect(y).to_be_within(kTolerance).of(model);
				expect(env.stage()).to_equal(Stage::SUSTAIN);
			}
		}
	});

	it("raises MPE's target by its follow input", _ {
		const EnvelopeState s = state(EnvelopeMode::MPE, 32, EnvelopeFollow::Y);
		Envelope env;
		(void)env.note_on(false, q31(0.25), s);
		double model = 0.0;
		for (uint32_t b = 0; b < 100; ++b) {
			const double input = b / 99.0;
			const double target = std::clamp(0.25 + (0.75 * input), 0.0, 1.0);
			const uint32_t rate = target > model ? kAttack : kRelease;
			model = target + ((model - target) * std::exp2(-8.0 * rate * kBlock / kStageEnd));
			const double y = level_of(env.render(kBlock, kAttack, kDecay, q31(0.25), kRelease, s, q31(input)));
			expect(y).to_be_within(kTolerance).of(model);
		}
	});

	it("carries a decay on from where it is when its rate changes every block", _ {
		const EnvelopeState s = state(EnvelopeMode::ADSR, 32);
		Envelope env;
		(void)env.note_on(true, q31(0.3), s); // straight to decay at full level
		double x = 0.0;
		for (uint32_t b = 0; b < 60; ++b) {
			const uint32_t decay = (b % 2) == 0 ? rate_for(300) : rate_for(5000);
			x += static_cast<double>(decay) * kBlock / kStageEnd;
			const double expected = x < 1.0 ? 0.3 + (0.7 * falling(8.0, x)) : 0.3;
			const double y = level_of(env.render(kBlock, kAttack, decay, q31(0.3), kRelease, s, Q<31>{}));
			expect(y).to_be_within(kTolerance).of(expected);
		}
	});

	it("resumes from the level it had when retriggered mid-release, in every mode", _ {
		for (EnvelopeMode mode : {EnvelopeMode::ADSR, EnvelopeMode::AHD, EnvelopeMode::AR, EnvelopeMode::MPE}) {
			const EnvelopeState s = state(mode, 32);
			Envelope env;
			(void)env.note_on(false, q31(0.6), s);
			double before = 0.0;
			for (uint32_t b = 0; b < 80; ++b) {
				if (b == 40) {
					env.note_off(true);
				}
				before = level_of(env.render(kBlock, kAttack, kDecay, q31(0.6), kRelease, s, Q<31>{}));
			}
			if (env.stage() == Stage::OFF) {
				continue; // AHD has finished by now: nothing to resume
			}
			const Q<31> old = env.output_level();
			(void)env.note_on(false, q31(0.6), s);
			env.resume_attack(old);
			const double after = level_of(env.render(kBlock, kAttack, kDecay, q31(0.6), kRelease, s, Q<31>{}));
			to_be_at_least(expect(after), before - kTolerance);
			to_be_at_most(expect(after), before + 0.2);
		}
	});

	it("fast-releases along a raised cosine at twice the release rate or the rate given", _ {
		const EnvelopeState s = state(EnvelopeMode::ADSR, 32);
		Envelope env;
		(void)env.note_on(false, q31(0.5), s);
		double l0 = 0.0;
		for (uint32_t b = 0; b < 100; ++b) {
			l0 = level_of(env.render(kBlock, kAttack, kDecay, q31(0.5), kRelease, s, Q<31>{}));
		}
		env.unconditional_release(Stage::FAST_RELEASE, kSoftCullIncrement);
		const double lf = length(std::max(kSoftCullIncrement, 2 * kRelease));
		for (uint32_t b = 0; b < 8; ++b) {
			const double t = static_cast<double>((b + 1) * kBlock);
			const double expected = t < lf ? l0 * (0.5 + (0.5 * std::cos(std::numbers::pi * t / lf))) : 0.0;
			const double y = level_of(env.render(kBlock, kAttack, kDecay, q31(0.5), kRelease, s, Q<31>{}));
			expect(y).to_be_within(kTolerance).of(expected);
		}
		expect(env.stage()).to_equal(Stage::OFF);
	});

	it("is small: a voice holds four, and their bytes are the fast RAM a voice costs", _ {
		to_be_at_most(expect(sizeof(Envelope)), size_t{40});
	});

	it("slews a minute as it slews a second, at any block size", _ {
		for (const double seconds : {10.0, 60.0}) {
			for (const int32_t curve : {1, 4}) { // k = 0.25 and 1
				for (const uint32_t n : {1U, 32U, 128U}) {
					const EnvelopeState s = state(EnvelopeMode::AR, curve);
					const uint32_t rate = rate_for(static_cast<uint32_t>(seconds * 44100.0));
					Envelope env;
					(void)env.note_on(false, q31(0.75), s);
					const auto blocks = static_cast<uint32_t>(seconds * 44100.0) / n;
					double worst = 0.0;
					for (uint32_t b = 0; b < blocks; ++b) {
						const double y = level_of(env.render(n, rate, kDecay, q31(0.75), kRelease, s, Q<31>{}));
						if (b % (blocks / 16) == 0 || b + 1 == blocks) {
							const double octaves = curve * 0.25 * rate * (static_cast<double>(b + 1) * n) / kStageEnd;
							worst = std::max(worst, std::abs(y - (0.75 * (1.0 - std::exp2(-octaves)))));
						}
					}
					to_be_at_most(expect(worst), 0x1p-13);
				}
			}
		}
	});

	it("arrives at a sustain a few words away, at any block size", _ {
		const EnvelopeState s = state(EnvelopeMode::ADSR, 32);
		for (const uint32_t n : {1U, 32U, 128U}) {
			for (const int32_t words : {-9, -1, 1, 9}) {
				Envelope env;
				(void)env.note_on(true, q31(0.5), s);
				(void)env.render(n, kAttack, 1U << 23, q31(0.5), kRelease, s, Q<31>{}); // the decay ends: sustain
				const Q<31> target = Q<31>::from_raw(q31(0.5).raw() + words);
				for (int32_t b = 0; b < 600; ++b) {
					(void)env.render(n, kAttack, kDecay, target, kRelease, s, Q<31>{});
				}
				expect(env.output_level()).to_equal(target);
			}
		}
	});

	it("resumes an attack from the level it had, whatever the curve and mode", _ {
		for (EnvelopeMode mode : {EnvelopeMode::ADSR, EnvelopeMode::AHD, EnvelopeMode::AR, EnvelopeMode::MPE}) {
			for (int32_t curve = -50; curve <= 50; ++curve) {
				const EnvelopeState s = state(mode, curve);
				for (const double level : {0.01, 0.37, 0.9}) {
					Envelope env;
					(void)env.note_on(false, q31(0.5), s);
					env.resume_attack(q31(level));
					// A rate so slow that the block barely moves it: no jump from the level it had
					const double y = level_of(env.render(1, 1, kDecay, q31(0.5), 1, s, Q<31>{}));
					expect(y).to_be_within(0x1p-16).of(level);
				}
			}
		}
	});

	it("keeps a held AR note at level 0 alive, and frees it after release", _ {
		const EnvelopeState s = state(EnvelopeMode::AR, 32);
		Envelope env;
		(void)env.note_on(false, Q<31>{}, s);
		for (uint32_t b = 0; b < 50; ++b) {
			const Q<31> word = env.render(kBlock, kAttack, kDecay, Q<31>{}, kRelease, s, Q<31>{});
			to_be_at_least(expect(word.raw()), Q<31>::min().raw() + 1);
			expect(env.stage() == Stage::OFF).to_equal(false);
		}
		env.note_off(true);
		for (uint32_t b = 0; b < 100; ++b) {
			(void)env.render(kBlock, kAttack, kDecay, Q<31>{}, kRelease, s, Q<31>{});
		}
		expect(env.stage()).to_equal(Stage::OFF);
	});
});
// clang-format on

DELUGE_SPEC(envelope_curves_spec, envelope_curves_spec_body)
