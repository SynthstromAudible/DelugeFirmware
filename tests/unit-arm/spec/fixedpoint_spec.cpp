#include "util/fixedpoint.h"
#include <cppspec.hpp>
#include <format>
#include <iostream>

template <size_t FractionalBits>
using FixedPointAccurate = FixedPoint<FractionalBits, false, false>;

template <size_t FractionalBits, bool Rounded, bool FastApproximation>
std::ostream& operator<<(std::ostream& os, const FixedPoint<FractionalBits, Rounded, FastApproximation>& fp) {
	os << std::format("<FixedPoint<{}>:0x{:x} = {}>", FractionalBits, fp.raw(), static_cast<float>(fp));
	return os;
}

// clang-format off
describe fixedpoint("FixedPoint", ${
	context("constructs", _ {
		it("from an integer", _ {
			FixedPoint<16> fp{42};
			expect(fp.raw()).to_equal(0x002a0000);
		});

		it("from a float", _ {
			FixedPoint<16> fp{3.14f};
			expect(fp.raw()).to_equal(0x000323d7);
		});

		it("from a different float", _ {
			FixedPoint<31> fp{0.5f};
			expect(fp.raw()).to_equal(0x40000000);
		});
	});

	context("copy constructor", _ {
		it("converts to a different FixedPoint with a different number of fractional bits", _ {
			FixedPoint<16> fp{42.25f};
			FixedPoint<24> fp2{fp};
			expect(fp2).to_equal(42.25f);
		});

		it("saturates if the value is too large", _ {
			FixedPoint<30> fp{2.0f};
			FixedPoint<31> fp2{fp};
			expect(fp2).to_equal(1.0f);
			expect(fp2.raw()).to_equal(0x7fffffff);
		});
	});

	context("==", _ {
		it("the original integer", _ {
			FixedPoint<16> fp{42};
			expect(fp).to_equal(42);
		});

		it("the original float", _ {
			FixedPoint<16> fp{3.14f};
			expect(fp).to_equal(3.14f);
		});

		it("another of equal value and the same number of fractional bits", _ {
			FixedPoint<16> fp1{3.14f};
			FixedPoint<16> fp2{3.14f};
			expect(fp1).to_equal(fp2);
		});

		it("another of equal value and different number of fractional bits", _ {
			FixedPoint<16> fp1{3.14f};
			FixedPoint<28> fp2{3.14f};
			expect(fp1).to_equal(fp2);
		});
	});

	context("<=>", _ {
		context("with the same number of fractional bits", _{
			it("to another of different value", _ {
				FixedPoint<16> fp1{3.14f};
				FixedPoint<16> fp2{2.71f};
				expect(fp2).to_be_less_than(fp1);
				expect(fp1).to_be_greater_than(fp2);
			});

			it("to another differing only in the fractional value", _ {
				FixedPoint<16> fp1{3.14f};
				FixedPoint<16> fp2{3.17f};
				expect(fp1).to_be_less_than(fp2);
				expect(fp2).to_be_greater_than(fp1);
			});
		});

		context("with different number of fractional bits", _{
			it("to another of different value", _ {
				FixedPoint<16> fp1{3.14f};
				FixedPoint<28> fp2{2.71f};
				expect(fp2).to_be_less_than(fp1);
				expect(fp1).to_be_greater_than(fp2);
			});

			it("to another differing only in the fractional value", _ {
				FixedPoint<16> fp1{3.14f};
				FixedPoint<28> fp2{3.17f};
				expect(fp1).to_be_less_than(fp2);
				expect(fp2).to_be_greater_than(fp1);
			});
		});
	});

	context("add", _ {
		it("adds another FixedPoint with an integral value", _ {
			FixedPoint<16> fp1{42};
			FixedPoint<16> fp2{2};
			expect(fp1 + fp2).to_equal(44);
		});

		it("adds another FixedPoint with a fractional value", _ {
			FixedPoint<16> fp1{42};
			FixedPoint<16> fp2{2.5f};
			expect(fp1 + fp2).to_equal(44.5f);
		});
	});

	context("subtract", _ {
		it("subtracts another FixedPoint with an integral value", _ {
			FixedPoint<16> fp1{42};
			FixedPoint<16> fp2{2};
			expect(fp1 - fp2).to_equal(40);
		});

		it("subtracts another FixedPoint with a fractional value", _ {
			FixedPoint<16> fp1{42};
			FixedPoint<16> fp2{2.5f};
			expect(fp1 - fp2).to_equal(39.5f);
		});
	});

	context("multiply", _ {
		context("approximate", _{
			context("with FixedPoint with the same number of fractional bits on rhs", _{
				it("multiplies by another FixedPoint with an integral value", _ {
					FixedPoint<16> fp1{42};
					FixedPoint<16> fp2{2};
					expect(fp1 * fp2).to_equal(84);
				});

				it("multiplies by another FixedPoint with a fractional value", _ {
					FixedPoint<16> fp1{42};
					FixedPoint<16> fp2{2.5f};
					expect(fp1 * fp2).to_equal(105);
				});
			});

			context("with FixedPoint with a different number of fractional bits on rhs", _{
				it("multiplies by another FixedPoint with an integral value", _ {
					FixedPoint<16> fp1{42};
					FixedPoint<28> fp2{2};
					expect(fp1 * fp2).to_equal(84);
				});

				it("multiplies by another FixedPoint with a fractional value", _ {
					FixedPoint<16> fp1{42};
					FixedPoint<28> fp2{2.5f};
					expect(fp1 * fp2).to_equal(105);
				});
			});
		});

		context("accurate", _ {
			context("with FixedPoint with the same number of fractional bits on rhs", _{
				it("multiplies by another FixedPoint with an integral value", _ {
					FixedPointAccurate<16> fp1{42};
					FixedPointAccurate<16> fp2{2};
					expect(fp1 * fp2).to_equal(84);
				});

				it("multiplies by another FixedPoint with a fractional value", _ {
					FixedPointAccurate<16> fp1{42};
					FixedPointAccurate<16> fp2{2.5f};
					expect(fp1 * fp2).to_equal(105);
				});
			});

			context("with FixedPoint with a different number of fractional bits on rhs", _{
				it("multiplies by another FixedPoint with an integral value", _ {
					FixedPointAccurate<16> fp1{42};
					FixedPointAccurate<28> fp2{2};
					expect(fp1 * fp2).to_equal(84);
				});

				it("multiplies by another FixedPoint with a fractional value", _ {
					FixedPointAccurate<16> fp1{42};
					FixedPointAccurate<28> fp2{2.5f};
					expect(fp1 * fp2).to_equal(105);
				});
			});
		});
	});

	context("divide", _{
		it("divides by another FixedPoint with an integral value", _ {
			FixedPoint<16> fp1{42};
			FixedPoint<16> fp2{2};
			expect(fp1 / fp2).to_equal(21);
		});

		it("divides by another FixedPoint with a fractional value", _ {
			FixedPoint<16> fp1{42};
			FixedPoint<16> fp2{2.5f};
			expect(fp1 / fp2).to_equal(16.8f);
		});
	});

	context("MultiplyAdd", _ {
		context("with FixedPoint with the same number of fractional bits", _{
			it("multiplies and adds another FixedPoint with an integral value", _ {
				FixedPoint<16> fp1{42};
				FixedPoint<17> fp2{2};
				FixedPoint<17> fp3{3};
				expect(fp1.MultiplyAdd(fp2, fp3)).to_equal(48);
			});

			it("multiplies and adds another FixedPoint with a fractional value", _ {
				FixedPoint<16> fp1{42};
				FixedPoint<16> fp2{2.5f};
				FixedPoint<16> fp3{3.5f};
				expect(fp2 * fp3).to_equal(8.75f);
				expect(fp1.MultiplyAdd(fp2, fp3)).to_equal(50.75f);
			});

			it("multiplies and adds quickly", _{
				FixedPoint<30> fp1{0.5f};
				FixedPoint<31> fp2{0.25f};
				FixedPoint<31> fp3{0.75f};
				expect(fp1.MultiplyAdd(fp2, fp3)).to_equal(0.6875f);
			});
		});
	});
});

CPPSPEC_SPEC(fixedpoint);
