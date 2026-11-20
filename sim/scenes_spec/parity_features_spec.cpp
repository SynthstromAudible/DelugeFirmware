/// @file
/// @brief The community features' table against the legacy settings' own declarations: each feature's file name,
///        factory value and option values.

#include "deluge-project/features.h"
#include "model/settings/runtime_feature_settings.h"

#include "cppspec.hpp"

#include <cstddef>
#include <string>

#include "support/spec_main.h"

namespace {

/// The legacy settings with their declarations in reach.
struct LegacyFeatures : RuntimeFeatureSettings {
	using RuntimeFeatureSettings::settings;
};

} // namespace

// clang-format off
describe parity_features("the community features' table, against the legacy settings", ${
	it("names, defaults and lists the values of every legacy feature as the legacy init does", _ {
		LegacyFeatures legacy;
		legacy.init();
		expect(deluge::project::kFeatureCount).to_equal(static_cast<size_t>(RuntimeFeatureSettingType::MaxElement));
		for (size_t i = 0; i < legacy.settings.size(); ++i) {
			const RuntimeFeatureSetting& want = legacy.settings[i];
			const deluge::project::FeatureInfo& have = deluge::project::kFeatures[i];
			const std::string at = "feature " + std::to_string(i) + " " + std::string{want.xmlName};
			expect(static_cast<size_t>(have.id)).to_equal(i, at);
			expect(std::string{have.path}).to_equal(std::string{want.xmlName}, at);
			expect(static_cast<uint32_t>(have.default_value)).to_equal(want.value, at);
			expect(have.choices.size()).to_equal(want.options.size(), at);
			for (size_t option = 0; option < want.options.size(); ++option) {
				expect(want.options[option].value).to_equal(static_cast<uint32_t>(option), at);
			}
		}
	});
});
// clang-format on

DELUGE_SPEC(parity_features_spec, parity_features)
