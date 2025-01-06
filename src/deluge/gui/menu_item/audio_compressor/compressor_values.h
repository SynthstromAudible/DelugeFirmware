#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/params/param_set.h"
#include "util/fixedpoint.h"

namespace deluge::gui::menu_item::audio_compressor {

class CompressorValue : public DecimalWithoutScrolling {
	using DecimalWithoutScrolling::DecimalWithoutScrolling;
	void readCurrentValue() final {
		uint64_t value = getCompressorValue();
		this->setValue(value >> 24);
	}
	void writeCurrentValue() final {
		auto value = this->getValue();
		if (value >= kMaxKnobPos) {
			value = kMaxKnobPos - 1;
		}
		q31_t knobPos = lshiftAndSaturate<24>(value);
		setCompressorValue(knobPos);
	}
	virtual uint64_t getCompressorValue() = 0;
	virtual void setCompressorValue(q31_t value) = 0;
	[[nodiscard]] int32_t getMaxValue() const final { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }
	const char* getUnit() override { return "MS"; }
};

class Attack final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final { return (uint64_t)soundEditor.currentModControllable->compressor.getAttack(); }
	void setCompressorValue(q31_t value) final { soundEditor.currentModControllable->compressor.setAttack(value); }
	float getDisplayValue() final { return soundEditor.currentModControllable->compressor.getAttackMS(); }
};
class Release final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final {
		return (uint64_t)soundEditor.currentModControllable->compressor.getRelease();
	}
	void setCompressorValue(q31_t value) final { soundEditor.currentModControllable->compressor.setRelease(value); }
	float getDisplayValue() final { return soundEditor.currentModControllable->compressor.getReleaseMS(); }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 1; }
};
class Ratio final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final { return (uint64_t)soundEditor.currentModControllable->compressor.getRatio(); }
	void setCompressorValue(q31_t value) final { soundEditor.currentModControllable->compressor.setRatio(value); }
	float getDisplayValue() final { return soundEditor.currentModControllable->compressor.getRatioForDisplay(); }
	const char* getUnit() final { return " : 1"; }
};
class SideHPF final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final {
		return (uint64_t)soundEditor.currentModControllable->compressor.getSidechain();
	}
	void setCompressorValue(q31_t value) final { soundEditor.currentModControllable->compressor.setSidechain(value); }
	float getDisplayValue() final { return soundEditor.currentModControllable->compressor.getSidechainForDisplay(); }
	const char* getUnit() final { return "HZ"; }
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
