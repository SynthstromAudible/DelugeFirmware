#include "memory/empty_space_vector.h"

#include "cppspec.hpp"
#include <algorithm>
#include <array>
#include <random>
#include <vector>

namespace {

// Convenience: build a record without naming the members at every call site.
constexpr EmptySpaceRecord rec(uint32_t length, uint32_t address) {
	return EmptySpaceRecord{length, address};
}

} // namespace

// clang-format off
describe empty_space_vector("EmptySpaceVector", $ {
	// Each example gets a fresh, generously-sized backing buffer adopted via reset().
	static std::array<EmptySpaceRecord, 64> buffer;
	static EmptySpaceVector vec;

	before_each([] {
		vec.reset(buffer);
	});

	// 1. Sorted-insert ordering -------------------------------------------------------------------
	context("sorted insert", _{
		it("keeps records ascending by (length, address) regardless of insertion order", _{
			const EmptySpaceRecord records[] = {
			    rec(50, 0x1000), rec(10, 0x4000), rec(10, 0x2000), rec(30, 0x3000), rec(10, 0x9000),
			};
			for (auto const& r : records) {
				vec.insert(r);
			}
			expect(vec.size()).to_equal(5u);
			expect(std::ranges::is_sorted(vec)).to_be_true();
			// duplicate lengths ordered by address
			expect(vec[0]).to_equal(rec(10, 0x2000));
			expect(vec[1]).to_equal(rec(10, 0x4000));
			expect(vec[2]).to_equal(rec(10, 0x9000));
			expect(vec[3]).to_equal(rec(30, 0x3000));
			expect(vec[4]).to_equal(rec(50, 0x1000));
		});

		it("returns {iterator-to-element, true} at the correct slot", _{
			vec.insert(rec(10, 0x1000));
			vec.insert(rec(30, 0x1000));
			auto [iter, ok] = vec.insert(rec(20, 0x1000));
			expect(ok).to_be_true();
			expect(iter).to_equal(vec.begin() + 1);
			expect(*iter).to_equal(rec(20, 0x1000));
		});
	});

	// 2. lower_bound_by_length --------------------------------------------------------------------
	context("lower_bound_by_length", _{
		before_each([] {
			vec.insert(rec(10, 0x1000));
			vec.insert(rec(20, 0x2000));
			vec.insert(rec(20, 0x5000));
			vec.insert(rec(40, 0x3000));
		});

		it("finds the first element with an exactly matching length", _{
			auto iter = vec.lower_bound_by_length(20);
			expect(iter).to_equal(vec.begin() + 1);
			expect(*iter).to_equal(rec(20, 0x2000));
		});

		it("returns the next-larger element when between values", _{
			auto iter = vec.lower_bound_by_length(25);
			expect(*iter).to_equal(rec(40, 0x3000));
		});

		it("returns begin() when smaller than all", _{
			expect(vec.lower_bound_by_length(1)).to_equal(vec.begin());
		});

		it("returns end() when larger than all", _{
			expect(vec.lower_bound_by_length(1000)).to_equal(vec.end());
		});

		it("returns end() on an empty container", _{
			vec.clear();
			expect(vec.lower_bound_by_length(10)).to_equal(vec.end());
		});
	});

	// 3. lower_bound on full key, including subrange forms ----------------------------------------
	context("lower_bound (full key)", _{
		before_each([] {
			vec.insert(rec(10, 0x1000));
			vec.insert(rec(10, 0x3000));
			vec.insert(rec(20, 0x2000));
			vec.insert(rec(20, 0x6000));
		});

		it("orders by length then address", _{
			auto iter = vec.lower_bound(rec(10, 0x2000));
			expect(*iter).to_equal(rec(10, 0x3000));
		});

		it("returns the exact element when present", _{
			auto iter = vec.lower_bound(rec(20, 0x2000));
			expect(iter).to_equal(vec.begin() + 2);
		});

		it("supports the [from,to) subrange form used by markSpaceAsEmpty/alloc", _{
			// std::ranges::lower_bound over an iterator subrange, as the call sites do.
			auto from = vec.begin() + 1;
			auto to = vec.end();
			auto iter = std::ranges::lower_bound(from, to, rec(20, 0x0));
			expect(iter).to_equal(vec.begin() + 2);
		});
	});

	// 4. find -------------------------------------------------------------------------------------
	context("find", _{
		before_each([] {
			vec.insert(rec(10, 0x1000));
			vec.insert(rec(20, 0x2000));
			vec.insert(rec(30, 0x3000));
		});

		it("returns the element iterator on a hit", _{
			auto iter = vec.find(rec(20, 0x2000));
			expect(iter).to_equal(vec.begin() + 1);
		});

		it("returns end() on a miss (right length, wrong address)", _{
			expect(vec.find(rec(20, 0x9999))).to_equal(vec.end());
		});

		it("returns end() on a miss (absent length)", _{
			expect(vec.find(rec(25, 0x2000))).to_equal(vec.end());
		});
	});

	// 5. erase ------------------------------------------------------------------------------------
	context("erase", _{
		before_each([] {
			vec.insert(rec(10, 0x1000));
			vec.insert(rec(20, 0x2000));
			vec.insert(rec(30, 0x3000));
		});

		it("erase(iterator) returns the following iterator and keeps order", _{
			auto next = vec.erase(vec.begin() + 1);
			expect(vec.size()).to_equal(2u);
			expect(*next).to_equal(rec(30, 0x3000));
			expect(vec[0]).to_equal(rec(10, 0x1000));
			expect(vec[1]).to_equal(rec(30, 0x3000));
			expect(std::ranges::is_sorted(vec)).to_be_true();
		});

		it("erase(key) returns 1 and removes on a hit", _{
			expect(vec.erase(rec(20, 0x2000))).to_equal(1u);
			expect(vec.size()).to_equal(2u);
			expect(vec.find(rec(20, 0x2000))).to_equal(vec.end());
		});

		it("erase(key) returns 0 and does not mutate on a miss", _{
			expect(vec.erase(rec(99, 0x9999))).to_equal(0u);
			expect(vec.size()).to_equal(3u);
		});

		it("erase(end()-1) returns end()", _{
			auto next = vec.erase(vec.end() - 1);
			expect(next).to_equal(vec.end());
			expect(vec.size()).to_equal(2u);
		});
	});

	// 6. std::rotate reposition both directions ---------------------------------------------------
	context("std::rotate reposition (call-site equivalence)", _{
		it("rotate-left moves a now-too-big element rightwards and stays sorted", _{
			// Mirrors markSpaceAsEmpty: an in-place record grew, std::rotate slides it right.
			vec.insert(rec(10, 0x1000)); // i = 0, this one "grows" to length 35
			vec.insert(rec(20, 0x2000));
			vec.insert(rec(30, 0x3000));
			vec.insert(rec(40, 0x4000));
			// Grow element 0 to 35 and reposition: rotate(begin()+i, begin()+i+1, begin()+insertBefore)
			vec[0] = rec(35, 0x1000);
			int insertBefore = 3; // first element with length >= 35, searched from i+2
			std::rotate(vec.begin() + 0, vec.begin() + 1, vec.begin() + insertBefore);
			expect(std::ranges::is_sorted(vec)).to_be_true();
			expect(vec[2]).to_equal(rec(35, 0x1000));
		});

		it("rotate-right moves a now-too-small element leftwards and stays sorted", _{
			// Mirrors alloc: a shrunk record slides left. rotate(begin()+insertAt, begin()+i, begin()+i+1)
			vec.insert(rec(10, 0x1000));
			vec.insert(rec(20, 0x2000));
			vec.insert(rec(30, 0x3000));
			vec.insert(rec(40, 0x4000)); // i = 3, shrinks to 15
			vec[3] = rec(15, 0x4000);
			int insertAt = 1; // first element with length >= 15, searched in [0, i)
			std::rotate(vec.begin() + insertAt, vec.begin() + 3, vec.begin() + 4);
			expect(std::ranges::is_sorted(vec)).to_be_true();
			expect(vec[1]).to_equal(rec(15, 0x4000));
		});
	});

	// 7. Graceful full ----------------------------------------------------------------------------
	context("graceful full", _{
		it("further inserts return {end(), false} and leave contents unchanged", _{
			std::array<EmptySpaceRecord, 4> small{};
			EmptySpaceVector v;
			v.reset(small);
			for (uint32_t i = 0; i < 4; i++) {
				auto [iter, ok] = v.insert(rec((i + 1) * 10, 0x1000 + i));
				expect(ok).to_be_true();
			}
			expect(v.full()).to_be_true();
			expect(v.size()).to_equal(4u);

			auto [iter, ok] = v.insert(rec(5, 0x9999));
			expect(ok).to_be_false();
			expect(iter).to_equal(v.end());
			expect(v.size()).to_equal(4u);
			// contents untouched
			expect(v[0]).to_equal(rec(10, 0x1000));
			expect(v[3]).to_equal(rec(40, 0x1003));
		});
	});

	// 8. Std-surface conformance ------------------------------------------------------------------
	context("std-surface conformance", _{
		it("exposes front/back/operator[]/data and size observers", _{
			vec.insert(rec(10, 0x1000));
			vec.insert(rec(30, 0x3000));
			vec.insert(rec(20, 0x2000));
			expect(vec.front()).to_equal(rec(10, 0x1000));
			expect(vec.back()).to_equal(rec(30, 0x3000));
			expect(vec[1]).to_equal(rec(20, 0x2000));
			expect(vec.data()).to_equal(vec.begin());
			expect(vec.empty()).to_be_false();
			expect(vec.size()).to_equal(3u);
			expect(vec.capacity()).to_equal(64u);
		});

		it("clear empties without touching capacity", _{
			vec.insert(rec(10, 0x1000));
			vec.clear();
			expect(vec.empty()).to_be_true();
			expect(vec.size()).to_equal(0u);
			expect(vec.capacity()).to_equal(64u);
		});

		it("is iterable with range-for and std::ranges algorithms", _{
			vec.insert(rec(10, 0x1000));
			vec.insert(rec(20, 0x2000));
			vec.insert(rec(30, 0x3000));
			uint32_t total = 0;
			for (auto const& r : vec) {
				total += r.length;
			}
			expect(total).to_equal(60u);
			auto found = std::ranges::find(vec, rec(20, 0x2000));
			expect(found).to_equal(vec.begin() + 1);
		});
	});

	// 9. Boundary ---------------------------------------------------------------------------------
	context("boundary", _{
		it("handles ops on an empty container", _{
			expect(vec.empty()).to_be_true();
			expect(vec.begin()).to_equal(vec.end());
			expect(vec.find(rec(10, 0x1000))).to_equal(vec.end());
			expect(vec.erase(rec(10, 0x1000))).to_equal(0u);
		});

		it("handles a single element", _{
			vec.insert(rec(10, 0x1000));
			expect(vec.size()).to_equal(1u);
			expect(vec.front()).to_equal(vec.back());
			vec.erase(vec.begin());
			expect(vec.empty()).to_be_true();
		});

		it("handles capacity() == 1", _{
			std::array<EmptySpaceRecord, 1> one{};
			EmptySpaceVector v;
			v.reset(one);
			expect(v.capacity()).to_equal(1u);
			expect(v.insert(rec(10, 0x1000)).second).to_be_true();
			expect(v.full()).to_be_true();
			expect(v.insert(rec(20, 0x2000)).second).to_be_false();
		});
	});

	// 10. Property / fuzz -------------------------------------------------------------------------
	context("property/fuzz vs sorted std::vector reference", _{
		it("matches a reference std::vector element-wise after every op", _{
			std::array<EmptySpaceRecord, 256> bigBuf{};
			EmptySpaceVector v;
			v.reset(bigBuf);
			std::vector<EmptySpaceRecord> reference;

			std::mt19937 rng(0xC0FFEE);
			std::uniform_int_distribution<uint32_t> lenDist(1, 50);
			std::uniform_int_distribution<uint32_t> addrDist(0x1000, 0x1100);
			std::uniform_int_distribution<int> opDist(0, 2);

			auto recordsEqual = [&] {
				if (v.size() != reference.size()) {
					return false;
				}
				return std::equal(v.begin(), v.end(), reference.begin());
			};

			for (int step = 0; step < 4000; step++) {
				int op = opDist(rng);
				if (op != 0 && !reference.empty()) {
					// erase a random existing key
					size_t idx = rng() % reference.size();
					EmptySpaceRecord key = reference[idx];
					v.erase(key);
					reference.erase(std::ranges::find(reference, key));
				}
				else if (!v.full()) {
					// insert (skip exact duplicates to keep multiset/reference math 1:1)
					EmptySpaceRecord r = rec(lenDist(rng), addrDist(rng));
					if (std::ranges::find(reference, r) == reference.end()) {
						v.insert(r);
						reference.push_back(r);
						std::ranges::sort(reference);
					}
				}
				expect(recordsEqual()).to_be_true();
				expect(std::ranges::is_sorted(v)).to_be_true();
			}
		});
	});
});
// clang-format on

CPPSPEC_SPEC(empty_space_vector);
