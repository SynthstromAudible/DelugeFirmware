#include "dsp-next/core/concepts.h"
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/noise/noise.h"
#include "util/synth_noise.h"
#include "util/waves.h" // the interim CONG, until every draw is injected

#include <cppspec.hpp>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

static_assert(noise_stream<Cong>);
static_assert(sample_generator<SeededNoise>);
static_assert(CongruentialStep::next(0) == 1234567u);
static_assert(Cong{0}.next() == 1234567u);
static_assert(sizeof(Cong) == sizeof(uint32_t));

namespace {
/// The reference sequence from a seed, computed directly from the step's recurrence.
std::vector<uint32_t> reference(uint32_t seed, std::size_t n) {
	std::vector<uint32_t> out;
	for (std::size_t i = 0; i < n; i++) {
		seed = (69069u * seed) + 1234567u;
		out.push_back(seed);
	}
	return out;
}
} // namespace

// clang-format off
describe noise("the synth's noise stream", ${
	it("steps a Cong through the step's sequence", _ {
		Cong cong{12345};
		std::vector<uint32_t> drawn;
		for (int i = 0; i < 8; i++) {
			drawn.push_back(cong.next());
		}
		expect(drawn).to_equal(reference(12345, 8));
		expect(cong.state()).to_equal(reference(12345, 8).back());
	});

	it("draws the step's sequence from a seed", _ {
		SeededNoise noise{12345};
		std::vector<uint32_t> drawn;
		for (int i = 0; i < 8; i++) {
			drawn.push_back(static_cast<uint32_t>(noise.render()));
		}
		expect(drawn).to_equal(reference(12345, 8));
	});

	it("keeps one sequence when a block copy is written back", _ {
		deluge::synth_noise.seed(777);
		Cong local = deluge::synth_noise;
		std::vector<uint32_t> drawn{local.next(), local.next()};
		deluge::synth_noise = local;
		drawn.push_back(static_cast<uint32_t>(CONG));
		expect(drawn).to_equal(reference(777, 3));
	});

	it("is seeded through the harness entry point", _ {
		deluge_seed_noise(42);
		expect(deluge::synth_noise.state()).to_equal(42u);
	});
});
// clang-format on

CPPSPEC_MAIN(noise);
