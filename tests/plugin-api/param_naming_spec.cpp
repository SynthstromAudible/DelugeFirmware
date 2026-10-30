#include "plugin-api/types/param_naming.h"

#include <array>
#include <cppspec.hpp>
#include <ostream>

using namespace deluge::plugin;

namespace deluge::plugin {
/// Failures print a ref by its names.
std::ostream& operator<<(std::ostream& os, const ParamRef& ref) {
	return os << '{' << ref.param << ", " << ref.source << ", " << ref.second_source << '}';
}
} // namespace deluge::plugin

namespace {
std::optional<ParamId> noId(const ParamRef&) {
	return std::nullopt;
}
ParamRef noRef(ParamId) {
	return {};
}
constexpr PluginId kOther{"test.other"};
constexpr std::array<ParamNaming, 2> kNamings{{
    {kDelugeInstrument, &noId, &noRef, nullptr},
    {kGlobalEffectable, &noId, &noRef, nullptr},
}};
} // namespace

// clang-format off
describe param_naming("how a plugin names its params in files", ${
	it("finds a naming by its plugin", _ {
		expect(naming_for(kNamings, kGlobalEffectable)).to_equal(&kNamings[1]);
		expect(naming_for(kNamings, kDelugeInstrument)).to_equal(&kNamings[0]);
	});

	it("finds none for a plugin it does not hold", _ {
		expect(naming_for(kNamings, kOther)).to_be_null();
	});

	it("compares refs by their names", _ {
		const ParamRef a{"pitch", "lfo1"};
		expect(a).to_equal(ParamRef{"pitch", "lfo1"});
		expect(a).not_().to_equal(ParamRef{"pitch"});
	});
});

CPPSPEC_SPEC(param_naming)
