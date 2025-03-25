#pragma once
#include "definitions_cxx.hpp"
#include "dsp/compressor/rms_feedback.h"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
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

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);

					setCompressorValue(knobPos, &soundDrum->compressor);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			setCompressorValue(knobPos, &soundEditor.currentModControllable->compressor);
		}
	}
	virtual uint64_t getCompressorValue() = 0;
	virtual void setCompressorValue(q31_t value, RMSFeedbackCompressor* compressor) = 0;
	[[nodiscard]] int32_t getMaxValue() const final { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }
	const char* getUnit() override { return "MS"; }
};

class Attack final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final { return (uint64_t)soundEditor.currentModControllable->compressor.getAttack(); }
	void setCompressorValue(q31_t value, RMSFeedbackCompressor* compressor) final { compressor->setAttack(value); }
	float getDisplayValue() final { return soundEditor.currentModControllable->compressor.getAttackMS(); }
};
class Release final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final {
		return (uint64_t)soundEditor.currentModControllable->compressor.getRelease();
	}
	void setCompressorValue(q31_t value, RMSFeedbackCompressor* compressor) final { compressor->setRelease(value); }
	float getDisplayValue() final { return soundEditor.currentModControllable->compressor.getReleaseMS(); }
	[[nodiscard]] int32_t getNumDecimalPlaces() const final { return 1; }
};
class Ratio final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final { return (uint64_t)soundEditor.currentModControllable->compressor.getRatio(); }
	void setCompressorValue(q31_t value, RMSFeedbackCompressor* compressor) final { compressor->setRatio(value); }
	float getDisplayValue() final { return soundEditor.currentModControllable->compressor.getRatioForDisplay(); }
	const char* getUnit() final { return " : 1"; }
};
class SideHPF final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final {
		return (uint64_t)soundEditor.currentModControllable->compressor.getSidechain();
	}
	void setCompressorValue(q31_t value, RMSFeedbackCompressor* compressor) final { compressor->setSidechain(value); }
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
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto value = this->getValue();

		q31_t knobPos;
		if (value < kMaxKnobPos) {
			knobPos = lshiftAndSaturate<24>(value);
		}
		else {
			knobPos = ONE_Q31;
		}

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);

					soundDrum->compressor.setBlend(knobPos);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentModControllable->compressor.setBlend(knobPos);
		}
	}
	int32_t getDisplayValue() override { return soundEditor.currentModControllable->compressor.getBlendForDisplay(); }
	const char* getUnit() override { return "%"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
};
} // namespace deluge::gui::menu_item::audio_compressor
