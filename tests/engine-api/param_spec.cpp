#include "engine-api/types/engine_id.h"
#include "engine-api/types/param.h"

#include <array>
#include <cppspec.hpp>
#include <limits>

using namespace deluge::engine;

namespace {
constexpr std::array<ParamInfo, 3> kTable{{
    {.id = ParamId{0}, .file_name = "volume", .scope = ParamScope::PER_VOICE},
    {.id = ParamId{7}, .file_name = "pan", .bipolar = true},
    {.id = ParamId{90}, .file_name = "tempo"},
}};
} // namespace

// clang-format off
describe param("an engine's param words", ${
	it("orders and compares ids by number", _ {
		expect(ParamId{3} == ParamId{3}).to_equal(true);
		expect(ParamId{3} < ParamId{90}).to_equal(true);
	});

	it("spans the whole stored word unless a row says otherwise", _ {
		ParamRange range{};
		expect(range.min).to_equal(std::numeric_limits<int32_t>::min());
		expect(range.max).to_equal(std::numeric_limits<int32_t>::max());
	});

	it("finds a row by id and by file name", _ {
		expect(find_param(kTable, ParamId{7})->file_name == "pan").to_equal(true);
		expect(find_param(kTable, "tempo")->id == ParamId{90}).to_equal(true);
		expect(find_param(kTable, ParamId{8}) == nullptr).to_equal(true);
		expect(find_param(kTable, "none") == nullptr).to_equal(true);
		expect(find_param(kTable, "") == nullptr).to_equal(true);
	});

	it("knows which ids a block holds", _ {
		ParamBlock block{ParamId{90}, 27};
		expect(block.contains(ParamId{90})).to_equal(true);
		expect(block.contains(ParamId{116})).to_equal(true);
		expect(block.contains(ParamId{117})).to_equal(false);
		expect(block.contains(ParamId{89})).to_equal(false);
	});

	it("names the provisional global-effectable table", _ {
		expect(kGlobalEffectable == EngineId{"deluge.global-effectable"}).to_equal(true);
		expect(kGlobalEffectable == kDelugeInstrument).to_equal(false);
	});
});

CPPSPEC_SPEC(param)
