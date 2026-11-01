#include "param_mapping_reference.h"
#include "plugin-api/types/param_mapping.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>

using namespace deluge::plugin;
namespace ref = deluge::plugin::test::reference;

namespace {
/// Words a sweep can't miss: both ends, around zero and the half, and a deterministic spread between.
template <class F>
void each_word(F f) {
	for (int64_t v : {int64_t{INT32_MIN}, int64_t{INT32_MIN} + 1, int64_t{-1}, int64_t{0}, int64_t{1}, int64_t{1} << 30,
	                  (int64_t{1} << 30) - 1, int64_t{INT32_MAX} - (1 << 24), int64_t{INT32_MAX} - (1 << 24) - 1,
	                  int64_t{INT32_MAX} - (1 << 24) + 1, int64_t{INT32_MAX} - (1 << 24) + 2, int64_t{INT32_MAX}}) {
		f(static_cast<int32_t>(v));
	}
	uint32_t x = 0x9E3779B9u;
	for (int i = 0; i < 1'000'000; ++i) {
		x = x * 1664525u + 1013904223u;
		f(static_cast<int32_t>(x));
	}
}
} // namespace

// clang-format off
describe param_mapping("the param mapping kinds", ${
	it("converts words to knob positions as today", _{
		int32_t bad = 0;
		each_word([&](int32_t v) {
			bad += to_knob(MappingKind::STANDARD, v) != ref::standard_to_knob(v);
			bad += to_knob(MappingKind::PAN, v) != ref::standard_to_knob(v);
			bad += to_knob(MappingKind::UNIPOLAR, v) != ref::unipolar_to_knob(v);
			bad += to_knob(MappingKind::CABLE_DEPTH, v) != ref::cable_to_knob(v);
		});
		expect(bad).to_equal(0);
	});
	it("converts every knob position to a word as today", _{
		int32_t bad = 0;
		for (int32_t k = -64; k <= 64; ++k) {
			bad += from_knob(MappingKind::STANDARD, k) != ref::standard_from_knob(k);
			bad += from_knob(MappingKind::PAN, k) != ref::standard_from_knob(k);
			bad += from_knob(MappingKind::UNIPOLAR, k) != ref::unipolar_from_knob(k);
			bad += from_knob(MappingKind::CABLE_DEPTH, k) != ref::cable_from_knob(k);
		}
		expect(bad).to_equal(0);
	});
	it("converts words to menu numbers as today", _{
		int32_t bad = 0;
		each_word([&](int32_t v) {
			bad += to_menu(MappingKind::STANDARD, v) != ref::standard_to_menu(v);
			bad += to_menu(MappingKind::UNIPOLAR, v) != ref::half_to_menu(v);
			bad += to_menu(MappingKind::PAN, v) != ref::pan_to_menu(v);
			bad += to_menu(MappingKind::CABLE_DEPTH, v) != ref::cable_to_menu(v);
		});
		expect(bad).to_equal(0);
	});
	it("converts every menu number to a word as today", _{
		int32_t bad = 0;
		for (int32_t m = 0; m <= 50; ++m) {
			bad += from_menu(MappingKind::STANDARD, m) != ref::standard_from_menu(m);
			bad += from_menu(MappingKind::UNIPOLAR, m) != ref::half_from_menu(m);
		}
		for (int32_t m = -25; m <= 25; ++m) {
			bad += from_menu(MappingKind::PAN, m) != ref::pan_from_menu(m);
		}
		for (int32_t m = menu_min(MappingKind::CABLE_DEPTH); m <= menu_max(MappingKind::CABLE_DEPTH); ++m) {
			bad += from_menu(MappingKind::CABLE_DEPTH, m) != ref::cable_from_menu(m);
		}
		expect(bad).to_equal(0);
	});
	it("turns a preset number into a word without the menu's end snap", _{
		int32_t bad = 0;
		for (int32_t u = -128; u <= 127; ++u) {
			bad += preset_word(static_cast<int8_t>(u)) != ref::preset_word(static_cast<int8_t>(u));
		}
		expect(bad).to_equal(0);
		expect(preset_word(50)).to_equal(2147483602); // not INT32_MAX: Sound's sustain-50 default depends on it
	});
});
// clang-format on

CPPSPEC_SPEC(param_mapping)
