#include "engines/effects/kernels.h"

#include <cppspec.hpp>
#include <cstdint>

using namespace deluge::effects;

// clang-format off
describe kernels("the fixed-point kernels the effects share", ${
	it("pans nothing at the centre", _ {
		const PanAmplitudes centre = pan_amplitudes(0);
		expect(centre.left).to_equal(1073741823);
		expect(centre.right).to_equal(1073741823);
	});
	it("pans fully left and right at the ends", _ {
		expect(pan_amplitudes(1073741824).left).to_equal(0);
		expect(pan_amplitudes(1073741824).right).to_equal(1073741823);
		expect(pan_amplitudes(-1073741824).right).to_equal(0);
	});
	it("doubles the exp curve per 2^26 of adjustment", _ {
		expect(exp_of(4194304, 1 << 26)).to_equal(2 * exp_of(4194304, 0));
	});
	it("keeps a sample's sign and remembers its working value", _ {
		uint32_t a = 2147483648u;
		uint32_t b = 2147483648u;
		expect(tanh_antialiased(1 << 28, a, 5) > 0).to_be_true();
		expect(tanh_antialiased(-(1 << 28), b, 5) < 0).to_be_true();
		expect(a).not_().to_equal(2147483648u);
	});
});

CPPSPEC_SPEC(kernels)
