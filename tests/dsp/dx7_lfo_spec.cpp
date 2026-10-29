// dx7::Lfo through Control, against the reference patch's LFO for every waveform at four rates over 2000 blocks of
// every length, with key sync at block 500 and a rate change at block 1000; then the waveform quirks.

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dx7/lfo.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dx7_workloads.h"
#include "reference_dx7.h"
#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

namespace dx7 = deluge::dsp::fixed::dx7;
using deluge::dsp::Control;

namespace {

constexpr std::size_t kBlocks = 2000;
constexpr std::size_t kKeySyncBlock = 500;
constexpr std::size_t kRateChangeBlock = 1000;
constexpr std::size_t kLfoRate = 137;
constexpr std::size_t kLfoWave = 142;
constexpr std::array<uint8_t, 4> kRates{0, 1, 35, 99};

} // namespace

describe dx7_lfo_spec(
    "dx7::Lfo", $ {
	    static dx7::Tables tables;
	    static dx7_reference::Tables reference_tables;
	    dx7::generate(tables);
	    dx7_reference::init_tables(reference_tables);

	    it(
	        "steps and reads every waveform as the reference, through key sync and a rate change", _ {
		        const auto lengths = dx7_workloads::block_lengths_1_to_132();
		        for (uint8_t waveform = 0; waveform < 6; ++waveform) {
			        for (const uint8_t rate : kRates) {
				        static dx7_reference::Patch reference;
				        reference = {};
				        std::ranges::copy(deluge::instrument::kDx7InitVoice, reference.params);
				        reference.params[kLfoRate] = rate;
				        reference.params[kLfoWave] = waveform;
				        Control<dx7::Lfo> lfo;

				        for (std::size_t block = 0; block < kBlocks; ++block) {
					        if (block == kKeySyncBlock) {
						        reference.lfo_phase = (1U << 31) - 1;
						        lfo.on<&dx7::Lfo::key_sync>();
					        }
					        if (block == kRateChangeBlock) {
						        reference.params[kLfoRate] = static_cast<uint8_t>((rate + 50) % 100);
					        }
					        const uint16_t n = lengths[block % lengths.size()];
					        lfo.configure({reference.params[kLfoRate], waveform, &tables});
					        reference.computeLfo(reference_tables, n);
					        expect(lfo.advance(n)).to_equal(static_cast<int32_t>(reference.lfo_value));
					        expect(lfo.state().phase).to_equal(reference.lfo_phase);
					        expect(lfo.coefficients().delta).to_equal(reference.lfo_delta);
				        }
			        }
		        }
	        });

	    it(
	        "plays the sine for the triangle and holds the middle for sample and hold", _ {
		        const auto at = [&](uint8_t waveform) {
			        return dx7::Lfo::coefficients({.rate = 0, .waveform = waveform, .tables = &tables});
		        };
		        for (uint32_t i = 0; i < 1000; ++i) {
			        const uint32_t phase = i * 4294967U;
			        expect(dx7::Lfo::wave_value(at(0), phase)).to_equal(dx7::Lfo::wave_value(at(4), phase));
			        expect(dx7::Lfo::wave_value(at(5), phase)).to_equal(1 << 23);
		        }
	        });
    });

CPPSPEC_MAIN(dx7_lfo_spec);
