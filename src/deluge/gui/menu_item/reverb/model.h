#pragma once

#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"
#include "processing/engines/audio_engine.h"
#include <string_view>

namespace deluge::gui::menu_item::reverb {
class Model final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(util::to_underlying(AudioEngine::reverb.getModel())); }
	void writeCurrentValue() override {
		AudioEngine::reverb.setModel(static_cast<dsp::Reverb::Model>(this->getValue()));
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_FREEVERB), l10n::getView(STRING_FOR_MUTABLE),
		        l10n::getView(STRING_FOR_DIGITAL)};
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions &options) override {
		Selection::configureRenderingOptions(options);
		options.label = l10n::get(l10n::String::STRING_FOR_MODEL_SHORT);
	}
};
} // namespace deluge::gui::menu_item::reverb
