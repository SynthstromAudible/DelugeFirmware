#pragma once

#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "model/sample/sample_controls.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item {

class AudioInterpolation : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		const auto& sampleControls = getCurrentAudioClip()->sampleControls;
		setValue(sampleControls.interpolationMode);
	}

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		// M3 owner-4: interpolation is DOM-owned — funnel through the setter (record + render cache), not the cache
		// ref.
		getCurrentAudioClip()->setInterpolationMode(getValue<InterpolationMode>());
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {l10n::getView(l10n::String::STRING_FOR_LINEAR), l10n::getView(l10n::String::STRING_FOR_SINC)};
	}
};

} // namespace deluge::gui::menu_item
