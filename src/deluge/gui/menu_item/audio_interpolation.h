#pragma once

#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/sample/sample_controls.h"

namespace deluge::gui::menu_item {

class AudioInterpolation : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override { this->setValue(soundEditor.currentSampleControls->interpolationMode); }

	void writeCurrentValue() override {
		soundEditor.currentSampleControls->interpolationMode = this->getValue<InterpolationMode>();
	}

	deluge::vector<std::string_view> getOptions() override {
		return {l10n::getView(l10n::String::STRING_FOR_LINEAR), l10n::getView(l10n::String::STRING_FOR_SINC)};
	}
};

} // namespace deluge::gui::menu_item
