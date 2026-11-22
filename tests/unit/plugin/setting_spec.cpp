#include "plugin/types/setting.h"

#include <array>
#include <cppspec.hpp>

using namespace deluge::plugin;

namespace {
constexpr std::array<std::string_view, 2> kModes{"poly", "mono"};
constexpr std::array<SettingInfo, 2> kTable{{
    {.path = "polyphonic", .kind = SettingKind::CHOICE, .choices = kModes},
    {.path = "unison/num", .kind = SettingKind::INTEGER, .min = 1, .max = 8},
}};
} // namespace

// clang-format off
describe setting("a plugin's settings schema", ${
	it("finds a row by its path", _ {
		expect(find_setting(kTable, "unison/num")).to_equal(&kTable[1]);
		expect(find_setting(kTable, "polyphonic")->choices.size()).to_equal(size_t{2});
	});

	it("finds no row for a path it does not hold", _ {
		expect(find_setting(kTable, "unison/spread")).to_be_null();
	});

	it("reads the index a numbered path holds in its pattern's one `*`", _ {
		expect(index_in("cv/*/transpose", "cv/1/transpose").value_or(99)).to_equal(size_t{1});
		expect(index_in("cv/*/transpose", "cv/12/transpose").value_or(99)).to_equal(size_t{12});
		expect(index_in("gate/*/mode", "gate/0/mode").value_or(99)).to_equal(size_t{0});
		// Not the pattern, not a plain decimal index, or a pattern with no `*` or two: nothing.
		expect(index_in("cv/*/transpose", "cv/1/voltsPerOctave").has_value()).to_be_false();
		expect(index_in("cv/*/transpose", "cv//transpose").has_value()).to_be_false();
		expect(index_in("cv/*/transpose", "cv/-1/transpose").has_value()).to_be_false();
		expect(index_in("cv/*/transpose", "cv/01/transpose").has_value()).to_be_false();
		expect(index_in("cv/*/transpose", "cv/x/transpose").has_value()).to_be_false();
		expect(index_in("cv/1/transpose", "cv/1/transpose").has_value()).to_be_false();
		expect(index_in("*/*/transpose", "1/1/transpose").has_value()).to_be_false();
	});
});

CPPSPEC_SPEC(setting)
