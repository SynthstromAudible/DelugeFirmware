#pragma once

#include "gui/menu_item/selection.h"
#include "processing/engines/audio_engine.h"
#include "dsp/reverb/reverb_manager.hpp"

namespace deluge::gui::menu_item::reverb {
class MenuItemReverbModel final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(util::to_underlying(AudioEngine::reverb.get_model())); }
	void writeCurrentValue() override {
		AudioEngine::reverb.set_model(static_cast<dsp::reverb::Model>(this->getValue()));
	}

	std::vector<std::string_view> getOptions() override {
		return {
		    "Freeverb",
		    "Mutable",
		    "Plateau",
		};
	}
};
} // namespace deluge::gui::menu_item::reverb
