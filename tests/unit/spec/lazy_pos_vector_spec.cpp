#include "util/container/lazy_pos_vector.h"

#include "cppspec.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

// The heap and the structural-edit probe, as the specs see them: allocations are counted so a leak or an early
// allocation shows, and the probe counts every edit and those made outside an AudioExcludedSection. The real probe
// counts only on alpha and beta builds.
namespace {

struct Node {
	int32_t pos = 0;
	int32_t value = 0;
};
using Vec = deluge::LazyPosVector<Node>;

int32_t g_outstanding = 0;
int32_t g_edits = 0;
int32_t g_unguarded = 0;
bool g_failNextAllocation = false;

void resetCounts() {
	g_outstanding = 0;
	g_edits = 0;
	g_unguarded = 0;
	g_failNextAllocation = false;
}

int32_t asInt(Error error) {
	return static_cast<int32_t>(error);
}

} // namespace

namespace deluge::memory {
void* alloc_fast(std::size_t size, std::size_t align) {
	if (g_failNextAllocation) {
		g_failNextAllocation = false;
		return nullptr;
	}
	g_outstanding++;
	return std::aligned_alloc(align, (size + align - 1) / align * align);
}
void dealloc(void* ptr) {
	if (ptr != nullptr) {
		g_outstanding--;
		std::free(ptr);
	}
}
} // namespace deluge::memory

namespace deluge {
void noteStructuralEdit(StructuralContainer) {
	g_edits++;
	if (deluge_audio_excluded_depth() == 0) {
		g_unguarded++;
	}
}
} // namespace deluge

// clang-format off
describe lazy_pos_vector("LazyPosVector", $ {
	before_each([] { resetCounts(); });

	it("owns nothing until something is put in it, and reads as empty", _{
		{
			Vec v;
			expect(v.size()).to_equal(size_t{0});
			expect(v.empty()).to_be_true();
			expect(v.begin() == v.end()).to_be_true();
			expect(v.tryGet(0)).to_be_null();
			expect(v.tryGetLast()).to_be_null();
			expect(v.firstAtOrAfter(5)).to_equal(0);
			expect(v.find_key(5) == v.end()).to_be_true();
			int32_t terms[2] = {3, 7};
			int32_t found[2] = {-1, -1};
			v.searchDual(terms, found);
			expect(found[0]).to_equal(0);
			expect(found[1]).to_equal(0);
			expect(v.generateRepeats(10, 40)).to_be_true();
			expect(g_outstanding).to_equal(0);
		}
		expect(g_outstanding).to_equal(0);
	});

	it("makes its vector on the first insert, with the render excluded", _{
		Vec v;
		auto inserted = v.insertSorted(10);
		expect(inserted).to_have_value();
		expect(v.size()).to_equal(size_t{1});
		expect(v[0].pos).to_equal(10);
		expect(g_outstanding).to_be_greater_than(0);
		expect(g_edits).to_be_greater_than(0);
		expect(g_unguarded).to_equal(0);
	});

	it("keeps its vector when erased to empty, and frees it on clear", _{
		Vec v;
		(void)v.insertSorted(10);
		const int32_t whileFull = g_outstanding;
		v.erase(v.begin());
		expect(v.empty()).to_be_true();
		expect(g_outstanding).to_equal(whileFull);
		v.clear();
		expect(g_outstanding).to_equal(0);
		expect(g_unguarded).to_equal(0);
	});

	it("frees everything when destroyed", _{
		{
			Vec v;
			(void)v.insertSorted(1);
			(void)v.insertSorted(2);
		}
		expect(g_outstanding).to_equal(0);
	});

	it("reports a failed first insert and stays empty", _{
		Vec v;
		g_failNextAllocation = true;
		auto inserted = v.insertSorted(10);
		expect(inserted).not_().to_have_value();
		expect(asInt(inserted.error())).to_equal(asInt(Error::INSUFFICIENT_RAM));
		expect(v.empty()).to_be_true();
		expect(g_outstanding).to_equal(0);
	});

	it("shares a byte-copy's nodes until beenCloned, then owns its own", _{
		{
			Vec original;
			(void)original.insertSorted(1);
			(void)original.insertSorted(2);
			alignas(Vec) unsigned char raw[sizeof(Vec)];
			std::memcpy(raw, static_cast<void*>(&original), sizeof(Vec));
			Vec& copy = *std::launder(reinterpret_cast<Vec*>(raw));
			expect(copy.data()).to_equal(original.data());
			expect(asInt(copy.beenCloned())).to_equal(asInt(Error::NONE));
			expect(copy.data()).not_().to_equal(original.data());
			expect(copy.size()).to_equal(size_t{2});
			copy[0].value = 99;
			expect(original[0].value).to_equal(0);
			copy.~Vec();
		}
		expect(g_outstanding).to_equal(0);
		expect(g_unguarded).to_equal(0);
	});

	it("leaves an empty byte-copy empty when cloned, allocating nothing", _{
		Vec original;
		alignas(Vec) unsigned char raw[sizeof(Vec)];
		std::memcpy(raw, static_cast<void*>(&original), sizeof(Vec));
		Vec& copy = *std::launder(reinterpret_cast<Vec*>(raw));
		expect(asInt(copy.beenCloned())).to_equal(asInt(Error::NONE));
		expect(copy.empty()).to_be_true();
		expect(g_outstanding).to_equal(0);
		copy.~Vec();
	});

	it("detaches a byte-copy it couldn't clone, leaving the original's nodes alone", _{
		{
			Vec original;
			(void)original.insertSorted(1);
			alignas(Vec) unsigned char raw[sizeof(Vec)];
			std::memcpy(raw, static_cast<void*>(&original), sizeof(Vec));
			Vec& copy = *std::launder(reinterpret_cast<Vec*>(raw));
			g_failNextAllocation = true;
			expect(asInt(copy.beenCloned())).to_equal(asInt(Error::INSUFFICIENT_RAM));
			expect(copy.empty()).to_be_true();
			expect(original.size()).to_equal(size_t{1});
			copy.~Vec();
		}
		expect(g_outstanding).to_equal(0);
	});

	it("forgets a byte-copy's nodes on init, without freeing the original's", _{
		Vec original;
		(void)original.insertSorted(1);
		const int32_t owned = g_outstanding;
		alignas(Vec) unsigned char raw[sizeof(Vec)];
		std::memcpy(raw, static_cast<void*>(&original), sizeof(Vec));
		Vec& copy = *std::launder(reinterpret_cast<Vec*>(raw));
		copy.init();
		expect(copy.empty()).to_be_true();
		copy.~Vec();
		expect(g_outstanding).to_equal(owned);
		expect(original.size()).to_equal(size_t{1});
	});

	it("copies with cloneFrom, and cloning an empty one frees this one's vector", _{
		Vec source;
		(void)source.insertSorted(5);
		Vec v;
		expect(v.cloneFrom(&source)).to_be_true();
		expect(v.size()).to_equal(size_t{1});
		expect(v[0].pos).to_equal(5);
		Vec empty;
		const int32_t beforeEmptyClone = g_outstanding;
		expect(v.cloneFrom(&empty)).to_be_true();
		expect(v.empty()).to_be_true();
		expect(g_outstanding).to_be_less_than(beforeEmptyClone);
	});

	it("moves and swaps by handing over its vector", _{
		Vec a;
		(void)a.insertSorted(3);
		Vec b{std::move(a)};
		expect(a.empty()).to_be_true();
		expect(b.size()).to_equal(size_t{1});
		Vec c;
		c.swap(b);
		expect(b.empty()).to_be_true();
		expect(c[0].pos).to_equal(3);
		Vec d;
		(void)d.insertSorted(4);
		d = std::move(c);
		expect(d[0].pos).to_equal(3);
		expect(c.empty()).to_be_true();
		expect(g_unguarded).to_equal(0);
	});
});

CPPSPEC_SPEC(lazy_pos_vector)
