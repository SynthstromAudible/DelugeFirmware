// dx7::PitchEnvelope through Control, against the reference PitchEnv over every block length: start, release at
// block 60 and a patch edit at block 90, for the init voice, every rate, levels crossing both ways and a held level.

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dx7/pitch_envelope.h"
#include "dx7_workloads.h"
#include "reference_dx7.h"
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dx7 = deluge::dsp::fixed::dx7;
using deluge::dsp::Control;

namespace {

/// @brief One run: the bytes at note-on, and the bytes the patch is edited to at block 90.
struct Run {
	dx7_reference::EnvParams start;
	dx7_reference::EnvParams edited;
};

constexpr std::size_t kReleaseBlock = 60;
constexpr std::size_t kEditBlock = 90;

dx7::EnvelopeParams params_of(const dx7_reference::EnvParams& p) {
	return {{p.rates[0], p.rates[1], p.rates[2], p.rates[3]}, {p.levels[0], p.levels[1], p.levels[2], p.levels[3]}};
}

/// @brief Every rate a step on from @p p's, the levels reversed: the edit at block 90.
dx7_reference::EnvParams edited(const dx7_reference::EnvParams& p) {
	dx7_reference::EnvParams e{};
	for (int i = 0; i < 4; ++i) {
		e.rates[i] = static_cast<uint8_t>((p.rates[i] + 37) % 100);
		e.levels[i] = p.levels[3 - i];
	}
	return e;
}

std::vector<Run> runs() {
	std::vector<Run> r;
	const auto add = [&](dx7_reference::EnvParams p) { r.push_back({p, edited(p)}); };
	add({{99, 99, 99, 99}, {50, 50, 50, 50}}); // the init voice
	for (uint8_t rate = 0; rate < 100; ++rate) {
		add({{rate, rate, rate, rate}, {0, 50, 99, 50}});
	}
	add({{40, 60, 30, 50}, {99, 0, 99, 0}});   // levels crossing down then up
	add({{40, 60, 30, 50}, {0, 99, 0, 99}});   // up then down
	add({{70, 70, 70, 70}, {50, 50, 50, 99}}); // held at level 4's own value until release
	return r;
}

} // namespace

describe dx7_pitch_envelope_spec(
    "dx7::PitchEnvelope", $ {
	    dx7_reference::PitchEnv::init(44100);

	    it(
	        "has the reference's unit at 44.1 kHz",
	        _ { expect(dx7::PitchEnvelope::kUnit).to_equal(static_cast<int32_t>(dx7_reference::PitchEnv::unit_)); });

	    it(
	        "advances, releases and follows an edit as the reference over every block length", _ {
		        for (const Run& run : runs()) {
			        dx7_reference::EnvParams p = run.start;
			        dx7_reference::PitchEnv reference{};
			        Control<dx7::PitchEnvelope> envelope;

			        reference.set(p);
			        envelope.configure({params_of(p)});
			        envelope.on<&dx7::PitchEnvelope::start>();

			        std::size_t block = 0;
			        for (const uint16_t n : dx7_workloads::block_lengths_1_to_132()) {
				        if (block == kReleaseBlock) {
					        reference.keydown(p, false);
					        envelope.configure({params_of(p)});
					        envelope.on<&dx7::PitchEnvelope::release>();
				        }
				        if (block == kEditBlock) {
					        p = run.edited;
				        }
				        envelope.configure({params_of(p)});
				        expect(envelope.advance(n)).to_equal(reference.getsample(p, n));
				        const dx7::PitchEnvelope::State& s = envelope.state();
				        expect(s.level).to_equal(reference.level_);
				        expect(s.target).to_equal(static_cast<int32_t>(reference.targetlevel_));
				        expect(s.stage).to_equal(static_cast<int32_t>(reference.ix_));
				        expect(s.increment).to_equal(static_cast<int32_t>(reference.inc_));
				        expect(s.rising).to_equal(reference.rising_);
				        expect(s.down).to_equal(reference.down_);
				        ++block;
			        }
		        }
	        });
    });

CPPSPEC_MAIN(dx7_pitch_envelope_spec);
