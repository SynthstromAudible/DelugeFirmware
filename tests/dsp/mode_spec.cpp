#include "dsp-next/core/mode.h"

#include <cppspec.hpp>
#include <cstdint>

using namespace deluge::dsp;

namespace {
struct NoDefault {
	explicit NoDefault(int v) : value(v) {}
	int value;
};
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
	it("returns the body's value for every mode", _ {
		auto body = []<Shape M>() { return static_cast<int>(M) * 10; };
		expect(with_mode<Shapes>(Shape::SAW, body)).to_equal(0);
		expect(with_mode<Shapes>(Shape::SQUARE, body)).to_equal(10);
		expect(with_mode<Shapes>(Shape::TRIANGLE, body)).to_equal(20);
	});
	it("dispatches a one-mode list", _ {
		enum class Only : uint8_t { ONE };
		int calls = 0;
		const int got = with_mode<ModeList<Only::ONE>>(Only::ONE, [&]<Only>() {
			calls++;
			return 7;
		});
		expect(calls).to_equal(1);
		expect(got).to_equal(7);
	});
	it("returns a type with no default constructor", _ {
		const NoDefault got = with_mode<Shapes>(Shape::TRIANGLE, []<Shape M>() { return NoDefault{static_cast<int>(M)}; });
		expect(got.value).to_equal(2);
	});
	it("returns a reference that aliases the referred object", _ {
		int a = 1;
		int b = 2;
		int c = 3;
		int& got = with_mode<Shapes>(Shape::SQUARE, [&]<Shape M>() -> int& {
			if constexpr (M == Shape::SAW) {
				return a;
			}
			else if constexpr (M == Shape::SQUARE) {
				return b;
			}
			else {
				return c;
			}
		});
		expect(&got == &b).to_be_true();
	});
});

CPPSPEC_MAIN(mode);
