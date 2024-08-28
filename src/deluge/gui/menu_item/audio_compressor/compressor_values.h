#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/params/param_set.h"
#include "util/fixedpoint.h"

namespace deluge::gui::menu_item::audio_compressor {
class Attack final : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;
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
	float getDisplayValue() override { return soundEditor.currentModControllable->compressor.getAttackMS(); }
	const char* getUnit() override { return "MS"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 2; }
};
class Release final : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;
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
	float getDisplayValue() override { return soundEditor.currentModControllable->compressor.getReleaseMS(); }
	const char* getUnit() override { return "MS"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 1; }
};
class Ratio final : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;
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
	float getDisplayValue() override { return soundEditor.currentModControllable->compressor.getRatioForDisplay(); }
	const char* getUnit() override { return " : 1"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 2; }
};
class SideHPF final : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;
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
	float getDisplayValue() override { return soundEditor.currentModControllable->compressor.getSidechainForDisplay(); }
	const char* getUnit() override { return "HZ"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 2; }
};
class Blend final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getBlend();
		this->setValue(value >> 24);
	}
	void writeCurrentValue() override {
		auto value = this->getValue();
		if (value < kMaxKnobPos) {
			q31_t knobPos = lshiftAndSaturate<24>(value);
			soundEditor.currentModControllable->compressor.setBlend(knobPos);
		}
		else if (value == kMaxKnobPos) {
			soundEditor.currentModControllable->compressor.setBlend(ONE_Q31);
		}
	}
	int32_t getDisplayValue() override { return soundEditor.currentModControllable->compressor.getBlendForDisplay(); }
	const char* getUnit() override { return "%"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
};
} // namespace deluge::gui::menu_item::audio_compressor
