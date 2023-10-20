#pragma once
#include "dsp/reverb/reverb.hpp"
#include "gui/menu_item/toggle.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item::reverb::plateau {
class InputDiffusion final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override {  this->setValue(AudioEngine::reverb.reverb_as<dsp::reverb::Plateau>().settings().input_diffusion); }
	void writeCurrentValue() override { AudioEngine::reverb.reverb_as<dsp::reverb::Plateau>().enableInputDiffusion(this->getValue()); }
};
} // namespace deluge::gui::menu_item::reverb::plateau
