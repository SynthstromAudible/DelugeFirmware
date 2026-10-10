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

/// A structural mode: a shape and a polarity, compared member by member.
struct Voicing {
	Shape shape;
	bool inverted;
	constexpr bool operator==(const Voicing&) const = default;
};
using Voicings = ModeList<Voicing{Shape::SAW, false}, Voicing{Shape::SAW, true}, Voicing{Shape::SQUARE, true}>;

/// Whether `ModeList<M>` names a class.
template <auto M>
concept lists_as_mode = requires { typename ModeList<M>; };

static_assert(lists_as_mode<Shape::SAW> && lists_as_mode<Voicing{}>);
static_assert(!lists_as_mode<1>, "a mode is an enumerator or a structural class value, not a number");
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
	it("dispatches on a structural mode, matching every member", _ {
		for (const Voicing v : {Voicing{Shape::SAW, false}, Voicing{Shape::SAW, true}, Voicing{Shape::SQUARE, true}}) {
			int calls = 0;
			Voicing seen{};
			with_mode<Voicings>(v, [&]<Voicing M>() {
				calls++;
				seen = M;
			});
			expect(calls).to_equal(1);
			expect(seen.shape).to_equal(v.shape);
			expect(seen.inverted).to_equal(v.inverted);
		}
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
		expect(&got).to_equal(&b);
	});
});

CPPSPEC_MAIN(mode);
