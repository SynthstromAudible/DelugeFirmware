#include "dsp-next/core/concepts.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/shaping/wavefolder.h"
#include "reference_wavefold.h"

#include <cppspec.hpp>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

static_assert(processor<Wavefolder>);
static_assert(!std::is_polymorphic_v<Wavefolder>);

namespace {
/// A block of full-range noise, so every fold region is visited.
std::vector<q31> noise_block(uint32_t seed, std::size_t n) {
	SeededNoise noise{seed};
	std::vector<q31> out(n);
	for (auto& s : out) {
		s = noise.render();
	}
	return out;
}
} // namespace

// clang-format off
describe wavefolder("the wavefolder", ${
	it("folds every sample as the voice always has, at every amount", _ {
		int32_t mismatches = 0;
		for (q31 amount : {q31{0}, q31{1}, q31{1 << 20}, q31{0x20000000}, q31{0x40000000}, q31{0x7FFFFFFF}}) {
			auto expected = noise_block(9 + static_cast<uint32_t>(amount), 257);
			auto actual = expected;
			reference_wavefold::foldBufferPolyApproximation(expected.data(), expected.data() + expected.size(), amount);
			Wavefolder{amount}.render_block(actual, actual);
			mismatches += expected != actual ? 1 : 0;
		}
		expect(mismatches).to_equal(0);
	});

	it("renders a block as it renders each sample", _ {
		const auto in = noise_block(3, 64);
		std::vector<q31> block(64);
		Wavefolder folder{0x30000000};
		folder.render_block(in, block);
		int32_t mismatches = 0;
		for (std::size_t i = 0; i < in.size(); i++) {
			mismatches += block[i] != folder.render(in[i]) ? 1 : 0;
		}
		expect(mismatches).to_equal(0);
	});

	it("folds silence to silence", _ {
		expect(Wavefolder{0x40000000}.render(0)).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(wavefolder);
