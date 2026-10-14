#include "dsp-next/core/concepts.h"
#include "dsp-next/fixed/noise/noise.h"
#include "util/waves.h"

#include <cppspec.hpp>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

static_assert(sample_generator<SharedNoise>);
static_assert(sample_generator<SeededNoise>);
static_assert(CongruentialStep::next(0) == 1234567u);
static_assert(CongruentialStep::next(1) == 69069u + 1234567u);

namespace {
/// The reference sequence from a seed, computed directly from the step.
std::vector<q31> reference(uint32_t seed, std::size_t n) {
	std::vector<q31> out;
	for (std::size_t i = 0; i < n; i++) {
		seed = 69069u * seed + 1234567u;
		out.push_back(static_cast<q31>(seed));
	}
	return out;
}
} // namespace

// clang-format off
describe noise("the synth's noise stream", ${
	it("draws the step's sequence from a seed", _ {
		SeededNoise noise{12345};
		std::vector<q31> drawn;
		for (int i = 0; i < 8; i++) {
			drawn.push_back(noise.render());
		}
		expect(drawn).to_equal(reference(12345, 8));
	});
	it("draws the shared stream exactly as CONG does", _ {
		jcong = 12345;
		std::vector<q31> drawn;
		for (int i = 0; i < 8; i++) {
			drawn.push_back(SharedNoise{}.render());
		}
		expect(drawn).to_equal(reference(12345, 8));
	});
	it("shares one state with the app's CONG, in call order", _ {
		jcong = 777;
		std::vector<q31> drawn;
		for (int i = 0; i < 4; i++) {
			drawn.push_back(SharedNoise{}.render());
			drawn.push_back(static_cast<q31>(CONG));
		}
		expect(drawn).to_equal(reference(777, 8));
	});
});

CPPSPEC_MAIN(noise);
