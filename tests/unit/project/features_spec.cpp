#include "project/features.h"

#include "cppspec.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace deluge::project;

namespace {
/// Whether `feature_info(F)` is a constant expression: false for an id the table has no entry for.
template <Feature F>
concept resolves_at_compile_time =
    requires { typename std::integral_constant<size_t, feature_info(F).choices.size()>; };
} // namespace

// clang-format off
describe features("the community features' table", ${
	it("holds every feature at its id's index, each file name once", _ {
		std::set<std::string> seen;
		for (size_t at = 0; at < kFeatures.size(); ++at) {
			const FeatureInfo& info = kFeatures[at];
			expect(static_cast<size_t>(std::to_underlying(info.id))).to_equal(at);
			expect(&feature_info(info.id)).to_equal(&info);
			expect(seen.insert(std::string{info.path}).second).to_be_true(std::string{info.path} + " is listed twice");
		}
		expect(kFeatures.size()).to_equal(size_t{22});
	});

	it("resolves a valid id, and a constant-evaluated lookup of an id past the table does not compile", _ {
		static_assert(resolves_at_compile_time<Feature::QUANTIZE>);
		static_assert(!resolves_at_compile_time<static_cast<Feature>(kFeatureCount)>);
		constexpr const FeatureInfo& last = feature_info(Feature::SHORTCUT_OVERLAY);
		expect(&last).to_equal(&kFeatures.back());
		expect(feature_info(Feature::QUANTIZE).path).to_equal(std::string_view{"quantize"});
	});

	it("gives each feature a default among its values", _ {
		for (const FeatureInfo& info : kFeatures) {
			expect(static_cast<size_t>(info.default_value)).to_be_less_than(info.choices.size(), std::string{info.path});
		}
	});

	it("makes every feature but the sync-scaling action and the shortcut overlay an on/off one", _ {
		for (const FeatureInfo& info : kFeatures) {
			const bool own_choices = info.id == Feature::SYNC_SCALING_ACTION || info.id == Feature::SHORTCUT_OVERLAY;
			expect(is_toggle(info)).to_equal(!own_choices, std::string{info.path});
		}
		expect(feature_info(Feature::SYNC_SCALING_ACTION).choices.size()).to_equal(size_t{2});
		expect(feature_info(Feature::SHORTCUT_OVERLAY).choices.size()).to_equal(size_t{3});
		expect(static_cast<int>(feature_info(Feature::SHORTCUT_OVERLAY).default_value))
		    .to_equal(static_cast<int>(std::to_underlying(ShortcutOverlay::MENU_ONLY)));
	});

	it("finds a feature by its file name, and none by a name no feature has", _ {
		expect(find_feature("enableHorizontalMenus")).to_equal(&feature_info(Feature::HORIZONTAL_MENUS));
		expect(find_feature("EnableDX7Engine")).to_be_null(); // retired: the DX7 is built in
		expect(find_feature("legacyMenu")).to_be_null();
		expect(find_feature("")).to_be_null();
	});
});
// clang-format on

CPPSPEC_SPEC(features)
