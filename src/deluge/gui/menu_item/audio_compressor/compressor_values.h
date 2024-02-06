#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/params/param_set.h"
#include "util/fixedpoint.h"

namespace deluge::gui::menu_item::audio_compressor {
class Attack final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getAttack();
		this->setValue((value * (kMaxMenuValue * 2) + 2147483648) >> 32);
	}
	void writeCurrentValue() override {
		if (this->getValue() == kMaxMenuValue) {
			soundEditor.currentModControllable->compressor.setAttack(ONE_Q31);
		}
		else {
			q31_t knobPos = (uint32_t)this->getValue() * (2147483648 / kMidMenuValue) >> 1;
			soundEditor.currentModControllable->compressor.setAttack(knobPos);
		}
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
};
class Release final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getRelease();
		this->setValue((value * (kMaxMenuValue * 2) + 2147483648) >> 32);
	}
	void writeCurrentValue() override {
		if (this->getValue() == kMaxMenuValue) {
			soundEditor.currentModControllable->compressor.setRelease(ONE_Q31);
		}
		else {
			q31_t knobPos = (uint32_t)this->getValue() * (2147483648 / kMidMenuValue) >> 1;
			soundEditor.currentModControllable->compressor.setRelease(knobPos);
		}
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
};
class Ratio final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getRatio();
		this->setValue((value * (kMaxMenuValue * 2) + 2147483648) >> 32);
	}
	void writeCurrentValue() override {
		if (this->getValue() == kMaxMenuValue) {
			soundEditor.currentModControllable->compressor.setRatio(ONE_Q31);
		}
		else {
			q31_t knobPos = (uint32_t)this->getValue() * (2147483648 / kMidMenuValue) >> 1;
			soundEditor.currentModControllable->compressor.setRatio(knobPos);
		}
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
};
class SideHPF final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getSidechain();
		this->setValue((value * (kMaxMenuValue * 2) + 2147483648) >> 32);
	}
	void writeCurrentValue() override {
		if (this->getValue() == kMaxMenuValue) {
			soundEditor.currentModControllable->compressor.setSidechain(ONE_Q31);
		}
		else {
			q31_t knobPos = (uint32_t)this->getValue() * (2147483648 / kMidMenuValue) >> 1;
			soundEditor.currentModControllable->compressor.setSidechain(knobPos);
		}
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
};
} // namespace deluge::gui::menu_item::audio_compressor
