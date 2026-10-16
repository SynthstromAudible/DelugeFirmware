#pragma once

#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "model/edits/setting_edit.h"
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
		deluge::edits::audioClipSettingFor(*getCurrentAudioClip(), "linearInterpolation",
		                                   deluge::serialization::Value::makeNumber(
		                                       getValue<InterpolationMode>() == InterpolationMode::LINEAR ? 1 : 0));
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {l10n::getView(l10n::String::STRING_FOR_LINEAR), l10n::getView(l10n::String::STRING_FOR_SINC)};
	}
};

} // namespace deluge::gui::menu_item
