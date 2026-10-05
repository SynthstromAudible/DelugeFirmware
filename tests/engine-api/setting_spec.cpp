#include "engine-api/types/setting.h"

#include <array>
#include <cppspec.hpp>

using namespace deluge::engine;

namespace {
constexpr std::array<std::string_view, 2> kModes{"poly", "mono"};
constexpr std::array<SettingInfo, 2> kTable{{
    {.path = "polyphonic", .kind = SettingKind::CHOICE, .choices = kModes},
    {.path = "unison/num", .kind = SettingKind::INTEGER, .min = 1, .max = 8},
}};
} // namespace

// clang-format off
describe setting("an engine's settings schema", ${
	it("finds a row by its path", _ {
		expect(find_setting(kTable, "unison/num")).to_equal(&kTable[1]);
		expect(find_setting(kTable, "polyphonic")->choices.size()).to_equal(size_t{2});
	});

	it("finds no row for a path it does not hold", _ {
		expect(find_setting(kTable, "unison/spread")).to_be_null();
	});
});

CPPSPEC_SPEC(setting)
