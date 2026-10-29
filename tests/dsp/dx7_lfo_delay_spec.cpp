// dx7::LfoDelay through Control, against the reference voice's LFO delay for every delay byte over 600 blocks of
// every length: the increments, the value and the position each block.

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dx7/lfo_delay.h"
#include "dx7_workloads.h"
#include "reference_dx7.h"
#include <algorithm>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

namespace dx7 = deluge::dsp::fixed::dx7;
using deluge::dsp::Control;

namespace {

constexpr std::size_t kBlocks = 600;
constexpr std::size_t kLfoDelay = 138;

} // namespace

describe dx7_lfo_delay_spec(
    "dx7::LfoDelay", $ {
	    static dx7_reference::Tables tables;
	    dx7_reference::init_tables(tables);

	    it("has the reference's LFO unit", _ { expect(dx7::LfoDelay::kLfoUnit).to_equal(dx7_reference::lfo_unit); });

	    it(
	        "delays and fades in as the reference for every delay over 600 blocks of every length", _ {
		        const auto lengths = dx7_workloads::block_lengths_1_to_132();
		        for (int delay = 0; delay < 100; ++delay) {
			        static dx7_reference::Patch patch;
			        static dx7_reference::Voice<dx7_workloads::SeededStatic> reference;
			        std::ranges::copy(deluge::instrument::kDx7InitVoice, patch.params);
			        patch.params[kLfoDelay] = static_cast<uint8_t>(delay);
			        reference = {};
			        reference.init(tables, patch, 60, 100);

			        Control<dx7::LfoDelay> lfo_delay;
			        lfo_delay.configure({static_cast<uint8_t>(delay)});
			        lfo_delay.reset();
			        expect(lfo_delay.coefficients().increment).to_equal(reference.delayinc_);
			        expect(lfo_delay.coefficients().settled_increment).to_equal(reference.delayinc2_);

			        for (std::size_t block = 0; block < kBlocks; ++block) {
				        const uint16_t n = lengths[block % lengths.size()];
				        expect(lfo_delay.advance(n)).to_equal(reference.getdelay(n));
				        expect(lfo_delay.state().position).to_equal(reference.delaystate_);
			        }
		        }
	        });
    });

CPPSPEC_MAIN(dx7_lfo_delay_spec);
