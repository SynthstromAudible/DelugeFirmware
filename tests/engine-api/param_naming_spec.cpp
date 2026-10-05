#include "engine-api/types/param_naming.h"

#include <array>
#include <cppspec.hpp>

using namespace deluge::engine;

namespace {
std::optional<ParamId> noId(const ParamRef&) {
	return std::nullopt;
}
ParamRef noRef(ParamId) {
	return {};
}
constexpr EngineId kOther{"test.other"};
constexpr std::array<ParamNaming, 2> kNamings{{
    {kDelugeInstrument, &noId, &noRef, nullptr},
    {kGlobalEffectable, &noId, &noRef, nullptr},
}};
} // namespace

// clang-format off
describe param_naming("how an engine names its params in files", ${
	it("finds a naming by its engine", _ {
		expect(naming_for(kNamings, kGlobalEffectable)).to_equal(&kNamings[1]);
		expect(naming_for(kNamings, kDelugeInstrument)).to_equal(&kNamings[0]);
	});

	it("finds none for an engine it does not hold", _ {
		expect(naming_for(kNamings, kOther)).to_be_null();
	});

	it("compares refs by their names", _ {
		expect(ParamRef{"pitch", "lfo1"} == ParamRef{"pitch", "lfo1"}).to_be_true();
		expect(ParamRef{"pitch", "lfo1"} == ParamRef{"pitch"}).to_be_false();
	});
});

CPPSPEC_SPEC(param_naming)
