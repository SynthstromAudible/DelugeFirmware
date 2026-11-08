#pragma once

/// @file
/// @brief One runtime feature setting set for a spec example and put back however the example ends.

#include "model/settings/runtime_feature_settings.h"

#include <cstdint>

namespace deluge::scenes::spec {

/// @brief Set one runtime feature setting for an example, and put it back.
struct FeatureSetting {
	RuntimeFeatureSettingType type;
	uint32_t saved;
	FeatureSetting(RuntimeFeatureSettingType t, uint32_t value) : type(t), saved(runtimeFeatureSettings.get(t)) {
		runtimeFeatureSettings.set(type, value);
	}
	~FeatureSetting() { runtimeFeatureSettings.set(type, saved); }
	FeatureSetting(const FeatureSetting&) = delete;
	FeatureSetting& operator=(const FeatureSetting&) = delete;
};

} // namespace deluge::scenes::spec
