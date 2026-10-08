#include "dsp-next/core/mode.h"

#include <array>
#include <cppspec.hpp>

using namespace deluge::dsp;

namespace {
enum class Shape : uint8_t { SAW, SQUARE, TRIANGLE };
using Shapes = ModeList<Shape::SAW, Shape::SQUARE, Shape::TRIANGLE>;
} // namespace

// clang-format off
describe mode("per-block mode dispatch", ${
	it("calls the body once, instantiated for the runtime mode", _ {
		for (Shape s : {Shape::SAW, Shape::SQUARE, Shape::TRIANGLE}) {
			int calls = 0;
			Shape seen{};
			with_mode<Shapes>(s, [&]<Shape M>() {
				calls++;
				seen = M;
			});
			expect(calls).to_equal(1);
			expect(seen).to_equal(s);
		}
	});
	it("returns the body's value", _ {
		const int got = with_mode<Shapes>(Shape::SQUARE, []<Shape M>() { return static_cast<int>(M) * 10; });
		expect(got).to_equal(10);
	});
	it("dispatches a one-mode list", _ {
		enum class Only : uint8_t { ONE };
		int calls = 0;
		with_mode<ModeList<Only::ONE>>(Only::ONE, [&]<Only>() { calls++; });
		expect(calls).to_equal(1);
	});
});

CPPSPEC_MAIN(mode);
