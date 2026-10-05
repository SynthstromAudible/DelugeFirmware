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
		expect(ParamId{3}.value).to_equal(ParamId{3}.value);
		expect(ParamId{3} < ParamId{90}).to_be_true();
	});

	it("spans the whole stored word unless a row says otherwise", _ {
		ParamRange range{};
		expect(range.min).to_equal(std::numeric_limits<int32_t>::min());
		expect(range.max).to_equal(std::numeric_limits<int32_t>::max());
	});

	it("finds a row by id and by file name", _ {
		expect(find_param(kTable, ParamId{7})->file_name).to_equal(std::string_view("pan"));
		expect(find_param(kTable, "tempo")->id.value).to_equal(90u);
		expect(find_param(kTable, ParamId{8})).to_be_null();
		expect(find_param(kTable, "none")).to_be_null();
		expect(find_param(kTable, "")).to_be_null();
	});

	it("knows which ids a block holds", _ {
		ParamBlock block{ParamId{90}, 27};
		expect(block.contains(ParamId{90})).to_be_true();
		expect(block.contains(ParamId{116})).to_be_true();
		expect(block.contains(ParamId{117})).to_be_false();
		expect(block.contains(ParamId{89})).to_be_false();
	});

	it("names the provisional global-effectable table", _ {
		expect(kGlobalEffectable.name).to_equal(std::string_view("deluge.global-effectable"));
		expect(kGlobalEffectable.name).not_().to_equal(kDelugeInstrument.name);
	});
});

CPPSPEC_SPEC(param)
