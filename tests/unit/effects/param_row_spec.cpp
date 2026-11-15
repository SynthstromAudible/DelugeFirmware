#include "effects/param_row.h"
#include "effects/params.h"

#include <array>
#include <cppspec.hpp>
#include <limits>
#include <string_view>

using namespace deluge::effects;
using deluge::plugin::ParamFlag;
using deluge::plugin::ParamId;
using deluge::plugin::ParamInfo;
using deluge::plugin::ParamScope;

namespace {
constexpr std::array<ParamInfo, 2> kTable{{
    row(ParamId{118}, params::kDelayRate),
    row(ParamId{49}, params::kDelayFeedback,
        {.default_value = std::numeric_limits<int32_t>::min(),
         .scope = ParamScope::PER_VOICE,
         .extra_flags = ParamFlag::SMOOTHED}),
}};
constexpr std::array<ParamId, 2> kIds{ParamId{118}, ParamId{49}};
constexpr std::array<EffectParam, 2> kDelayPair{params::kDelayRate, params::kDelayFeedback};
static_assert(built_from(kTable, kIds, kDelayPair));
} // namespace

// clang-format off
describe param_row("an owner's row of an effect param", ${
	it("takes the name, mapping and flags from the effect", _ {
		expect(kTable[0].file_name).to_equal(std::string_view{"delayRate"});
		expect(kTable[0].flags.has(ParamFlag::MIDDLE_DETENT)).to_equal(true);
		expect(kTable[0].default_value).to_equal(0);
		expect(static_cast<int>(kTable[0].scope)).to_equal(static_cast<int>(ParamScope::PER_SOUND));
	});

	it("takes the id, default, scope and added flags from the owner", _ {
		expect(kTable[1].id.value).to_equal(49U);
		expect(kTable[1].default_value).to_equal(std::numeric_limits<int32_t>::min());
		expect(static_cast<int>(kTable[1].scope)).to_equal(static_cast<int>(ParamScope::PER_VOICE));
		expect(kTable[1].flags.has(ParamFlag::MIDDLE_DETENT)).to_equal(true);
		expect(kTable[1].flags.has(ParamFlag::SMOOTHED)).to_equal(true);
	});

	it("refuses an id list that names the effect's params out of order or names another row", _ {
		constexpr std::array<ParamId, 2> swapped{ParamId{49}, ParamId{118}};
		expect(built_from(kTable, swapped, kDelayPair)).to_equal(false);
		constexpr std::array<ParamId, 2> missing{ParamId{118}, ParamId{50}};
		expect(built_from(kTable, missing, kDelayPair)).to_equal(false);
		constexpr std::array<ParamId, 1> short_list{ParamId{118}};
		expect(built_from(kTable, short_list, kDelayPair)).to_equal(false);
	});

	it("refuses a row that drops a flag its effect param has", _ {
		constexpr std::array<ParamInfo, 1> bare{{{.id = ParamId{118}, .file_name = "delayRate"}}};
		constexpr std::array<ParamId, 1> ids{ParamId{118}};
		constexpr std::array<EffectParam, 1> rate{params::kDelayRate};
		expect(built_from(bare, ids, rate)).to_equal(false);
	});
});
// clang-format on

CPPSPEC_SPEC(param_row)
