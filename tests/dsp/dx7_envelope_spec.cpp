// dx7::Envelope through Control, against the reference Env over every block length: start, a retune with a new
// output level at block 40, release at block 70 and a rate edit at block 100, for each parameter set at three rate
// scalings and two extra rates; then the retune quirk and the mono hand-over.

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dx7/envelope.h"
#include "dx7_workloads.h"
#include "reference_dx7.h"
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace dx7 = deluge::dsp::fixed::dx7;
using deluge::dsp::Control;

namespace {

constexpr std::size_t kRetuneBlock = 40;
constexpr std::size_t kReleaseBlock = 70;
constexpr std::size_t kEditBlock = 100;
constexpr int32_t kOutputLevel = (28 + 90) << 5; // output level byte 90, as the voice scales it
constexpr int32_t kRetunedOutputLevel = 64 << 5; // a quieter key after retune
constexpr std::array<int32_t, 3> kRateScalings{0, 31, 63};
constexpr std::array<int32_t, 2> kExtraRates{0, 5};

dx7::EnvelopeParams params_of(const dx7_reference::EnvParams& p) {
	return {{p.rates[0], p.rates[1], p.rates[2], p.rates[3]}, {p.levels[0], p.levels[1], p.levels[2], p.levels[3]}};
}

/// @brief Every rate 37 steps on: the edit at block 100.
dx7_reference::EnvParams edited(dx7_reference::EnvParams p) {
	for (auto& rate : p.rates) {
		rate = static_cast<uint8_t>((rate + 37) % 100);
	}
	return p;
}

std::vector<dx7_reference::EnvParams> parameter_sets() {
	std::vector<dx7_reference::EnvParams> sets;
	sets.push_back({{99, 99, 99, 99}, {99, 99, 99, 0}}); // the init voice's op
	for (uint8_t rate = 0; rate < 100; ++rate) {
		sets.push_back({{rate, rate, rate, rate}, {99, 0, 99, 0}});
	}
	sets.push_back({{50, 50, 50, 50}, {0, 99, 50, 0}});   // an attack to level 0: the held path
	sets.push_back({{60, 60, 60, 60}, {80, 80, 80, 80}}); // levels equal to the level reached: held stages
	return sets;
}

} // namespace

// clang-format off
describe dx7_envelope_spec("dx7::Envelope", ${
	it("starts, retunes, releases and follows an edit as the reference over every block length", _ {
		const auto expect_state = [&](const dx7::Envelope::State& s, const dx7_reference::Env& reference) {
			expect(s.level).to_equal(reference.level_);
			expect(s.target).to_equal(static_cast<int32_t>(reference.targetlevel_));
			expect(s.stage).to_equal(static_cast<int32_t>(reference.ix_));
			expect(s.increment).to_equal(static_cast<int32_t>(reference.inc_));
			expect(s.hold).to_equal(static_cast<int32_t>(reference.staticcount_));
			expect(s.down).to_equal(reference.down_);
			expect(s.rising).to_equal(reference.rising_);
		};
		for (const dx7_reference::EnvParams& start : parameter_sets()) {
			for (const int32_t rate_scaling : kRateScalings) {
				for (const int32_t extra_rate : kExtraRates) {
					dx7_reference::EnvParams p = start;
					int32_t output_level = kOutputLevel;
					dx7_reference::Env reference{};
					Control<dx7::Envelope> envelope;
					const auto settings = [&](int32_t extra) {
						return dx7::Envelope::Settings{params_of(p), output_level, rate_scaling, extra};
					};

					reference.init(p, output_level, rate_scaling);
					envelope.configure(settings(0));
					envelope.on<&dx7::Envelope::start>();
					expect_state(envelope.state(), reference);

					std::size_t block = 0;
					for (const uint16_t n : dx7_workloads::block_lengths_1_to_132()) {
						if (block == kRetuneBlock) {
							output_level = kRetunedOutputLevel;
							reference.update(p, output_level, rate_scaling);
							envelope.configure(settings(0));
							envelope.on<&dx7::Envelope::retune>();
						}
						if (block == kReleaseBlock) {
							reference.keydown(p, false);
							envelope.configure(settings(0));
							envelope.on<&dx7::Envelope::release>();
						}
						if (block == kEditBlock) {
							p = edited(p);
						}
						envelope.configure(settings(extra_rate));
						expect(envelope.advance(n)).to_equal(reference.getsample(p, n, extra_rate));
						expect_state(envelope.state(), reference);
						++block;
					}
				}
			}
		}
	});

	it("retunes to stage 2's target with the new output level", _ {
		const dx7::EnvelopeParams params{{70, 70, 70, 70}, {99, 60, 40, 0}};
		Control<dx7::Envelope> envelope;
		envelope.configure({params, kOutputLevel, 0, 0});
		envelope.on<&dx7::Envelope::start>();
		std::ignore = envelope.advance(64);
		constexpr int32_t kLevel = 3000; // above the 16-microstep floor with or without stage 2's level
		envelope.configure({params, kLevel, 0, 0});
		envelope.on<&dx7::Envelope::retune>();
		const int32_t stage_2 = (dx7::Envelope::scale_output_level(40) >> 1) << 6;
		expect(envelope.state().stage).to_equal(2);
		expect(envelope.state().target).to_equal((stage_2 + kLevel - dx7::Envelope::kLevelOffset) << 16);
		expect(envelope.state().target).not_().to_equal(16 << 16); // the level-less target, floored
	});

	it("takes over another envelope's state whole", _ {
		const dx7::EnvelopeParams params{{30, 40, 50, 60}, {99, 50, 70, 0}};
		Control<dx7::Envelope> from;
		from.configure({params, kOutputLevel, 10, 0});
		from.on<&dx7::Envelope::start>();
		std::ignore = from.advance(100);
		Control<dx7::Envelope> to;
		to.on<&dx7::Envelope::transfer>(from.state());
		const dx7::Envelope::State& a = to.state();
		const dx7::Envelope::State& b = from.state();
		expect(a.level).to_equal(b.level);
		expect(a.target).to_equal(b.target);
		expect(a.stage).to_equal(b.stage);
		expect(a.increment).to_equal(b.increment);
		expect(a.hold).to_equal(b.hold);
		expect(a.down).to_equal(b.down);
		expect(a.rising).to_equal(b.rising);
	});
});
// clang-format on

CPPSPEC_MAIN(dx7_envelope_spec);
