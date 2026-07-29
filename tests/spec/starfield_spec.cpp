#include "hid/display/starfield.h"

#include "cppspec.hpp"
#include <cmath>

using deluge::hid::display::Starfield;

// Upper depth bound is kMaxDepth + 0.2, not kMaxDepth: scatter() spawns in
// [0.1, kMaxDepth + 0.1), matching the reference implementation's
// `lcg_f32(rng) * MAX_DEPTH + 0.1`.
static constexpr float kDepthCeiling = Starfield::kMaxDepth + 0.2f;

// clang-format off
describe starfield("Starfield", $ {
	it("puts every star in front of the viewer on construction", _ {
		Starfield field;
		for (size_t i = 0; i < Starfield::kNumStars; i++) {
			expect(field.depthOf(i)).to_be_greater_than(0.0f);
			expect(field.depthOf(i)).to_be_less_than(kDepthCeiling);
		}
	});

	it("is deterministic, so the visual harness is reproducible", _ {
		Starfield a;
		Starfield b;
		for (size_t i = 0; i < Starfield::kNumStars; i++) {
			expect(a.depthOf(i)).to_equal(b.depthOf(i));
			expect(a.project(i).x).to_equal(b.project(i).x);
			expect(a.project(i).y).to_equal(b.project(i).y);
		}
	});

	it("re-scatters without reseeding, so activations differ", _ {
		Starfield field;
		float firstDepths[Starfield::kNumStars];
		for (size_t i = 0; i < Starfield::kNumStars; i++) {
			firstDepths[i] = field.depthOf(i);
		}

		field.scatter();

		bool anyDifferent = false;
		for (size_t i = 0; i < Starfield::kNumStars; i++) {
			if (field.depthOf(i) != firstDepths[i]) {
				anyDifferent = true;
			}
		}
		expect(anyDifferent).to_be_true();
	});

	context("after thousands of frames", _ {
		it("never lets a star reach or pass the viewer", _ {
			// The projection divides by z, so z must stay strictly positive.
			Starfield field;
			for (int32_t frame = 0; frame < 5000; frame++) {
				field.advance();
				for (size_t i = 0; i < Starfield::kNumStars; i++) {
					expect(field.depthOf(i)).to_be_greater_than(0.0f);
					expect(field.depthOf(i)).to_be_less_than(kDepthCeiling);
				}
			}
		});

		it("never overflows the projected coordinates", _ {
			// Regression guard for the Rust-saturates / C++-is-UB cast difference:
			// the kMinDepth floor bounds factor at kFov/kMinDepth, keeping
			// coordinates far inside int32_t no matter how close a star gets.
			Starfield field;
			for (int32_t frame = 0; frame < 5000; frame++) {
				field.advance();
				for (size_t i = 0; i < Starfield::kNumStars; i++) {
					const Starfield::Projected p = field.project(i);
					expect(std::abs(p.x)).to_be_less_than(1000000);
					expect(std::abs(p.y)).to_be_less_than(1000000);
				}
			}
		});

		it("draws near stars as blocks and far stars as single pixels", _ {
			Starfield field;
			for (int32_t frame = 0; frame < 500; frame++) {
				field.advance();
				for (size_t i = 0; i < Starfield::kNumStars; i++) {
					const int32_t expectedSize =
					    (field.depthOf(i) < Starfield::kNearThreshold) ? 2 : 1;
					expect(field.project(i).size).to_equal(expectedSize);
				}
			}
		});
	});
});

CPPSPEC_SPEC(starfield)
