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
		this->setValue(value >> 24);
	}
	void writeCurrentValue() override {
		auto value = this->getValue();
		if (value < kMaxKnobPos) {
			q31_t knobPos = lshiftAndSaturate<24>(value);
			soundEditor.currentModControllable->compressor.setAttack(knobPos);
		}
	}
	int32_t getDisplayValue() override { return soundEditor.currentModControllable->compressor.getAttackMS(); }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
};
class Release final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getRelease();
		this->setValue(value >> 24);
	}
	void writeCurrentValue() override {
		auto value = this->getValue();
		if (value < kMaxKnobPos) {
			q31_t knobPos = lshiftAndSaturate<24>(value);
			soundEditor.currentModControllable->compressor.setRelease(knobPos);
		}
	}
	int32_t getDisplayValue() override { return soundEditor.currentModControllable->compressor.getReleaseMS(); }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
};
class Ratio final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getRatio();
		this->setValue(value >> 24);
	}
	void writeCurrentValue() override {
		auto value = this->getValue();
		if (value < kMaxKnobPos) {
			q31_t knobPos = lshiftAndSaturate<24>(value);
			soundEditor.currentModControllable->compressor.setRatio(knobPos);
		}
	}
	int32_t getDisplayValue() override { return soundEditor.currentModControllable->compressor.getRatioForDisplay(); }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
};
class SideHPF final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getSidechain();
		this->setValue(value >> 24);
	}
	void writeCurrentValue() override {
		auto value = this->getValue();
		if (value < kMaxKnobPos) {
			q31_t knobPos = lshiftAndSaturate<24>(value);
			soundEditor.currentModControllable->compressor.setSidechain(knobPos);
		}
	}
	int32_t getDisplayValue() override {
		return soundEditor.currentModControllable->compressor.getSidechainForDisplay();
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
};
} // namespace deluge::gui::menu_item::audio_compressor
