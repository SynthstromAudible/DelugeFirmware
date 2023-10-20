#pragma once
#include "dsp/reverb/reverb.hpp"
#include "dsp/reverb/valley/plateau.hpp"
#include "gui/menu_item/integer.h"
#include "processing/engines/audio_engine.h"
#include <cmath>

namespace deluge::gui::menu_item::reverb::plateau {
class Param : public Integer {
public:
	using Integer::Integer;
	virtual void set(float) = 0;
	virtual float get() = 0;

	void readCurrentValue() override { this->setValue(std::round(get() * scaler())); }
	void writeCurrentValue() override { set((float)this->getValue() / scaler()); }

	virtual bool isRelevant(Sound* sound, int32_t whichThing) {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::Plateau;
	}

	[[nodiscard]] int32_t getMaxValue() const override { return 50; }
	[[nodiscard]] virtual float getMaxFloat() const { return 1.f; }
	[[nodiscard]] virtual float getMinFloat() const { return 0.f; }
	[[nodiscard]] constexpr float scaler() const { return static_cast<float>(getMaxValue()) / getMaxFloat(); }

	[[nodiscard]] static deluge::dsp::reverb::Plateau& reverb()  { return AudioEngine::reverb.reverb_as<dsp::reverb::Plateau>(); }
};
}
