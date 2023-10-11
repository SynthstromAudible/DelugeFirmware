#pragma once

#include "dsp/reverb/reverb.hpp"
#include "gui/menu_item/selection.h"
#include "processing/engines/audio_engine.h"
namespace deluge::gui::menu_item::reverb {
class Model final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(util::to_underlying(AudioEngine::reverb.getModel())); }
	void writeCurrentValue() override {
		AudioEngine::reverb.setModel(static_cast<dsp::Reverb::Model>(this->getValue()));
	}

	std::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_FREEVERB),
		    l10n::getView(STRING_FOR_MUTABLE),
		    l10n::getView(STRING_FOR_LEXICON_PLATE),
		};
	}
};
} // namespace deluge::gui::menu_item::reverb
