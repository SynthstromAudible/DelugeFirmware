#pragma once
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/setting_edit.h"
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
protected:
	/// The compressor settings of the owner this menu shows.
	[[nodiscard]] static const deluge::effects::CompressorState& shown() {
		return soundEditor.currentModControllable->effects().compressor;
	}

public:
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

					setCompressorValue(knobPos, *soundDrum);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			setCompressorValue(knobPos, *soundEditor.currentModControllable);
		}
	}
	virtual uint64_t getCompressorValue() = 0;
	virtual void setCompressorValue(q31_t value, ModControllableAudio& owner) = 0;
	[[nodiscard]] int32_t getMaxValue() const final { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }
	const char* getUnit() override { return "MS"; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }
};

class Attack final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() override { return static_cast<uint64_t>(shown().attack); }
	void setCompressorValue(q31_t value, ModControllableAudio& owner) override {
		effectSettingFor(owner, "audioCompressor/attack", deluge::serialization::Value::makeNumber(value));
	}
	float getDisplayValue() override { return deluge::dsp::fixed::compressor_attack_ms(shown().attack); }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return ATTACK; }
	void getColumnLabel(etl::istring& label) override {
		label.append(l10n::get(l10n::String::STRING_FOR_ATTACK_SHORT));
	}
};
class Release final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final { return static_cast<uint64_t>(shown().release); }
	void setCompressorValue(q31_t value, ModControllableAudio& owner) final {
		effectSettingFor(owner, "audioCompressor/release", deluge::serialization::Value::makeNumber(value));
	}
	float getDisplayValue() override { return deluge::dsp::fixed::compressor_release_ms(shown().release); }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return RELEASE; }

	void getColumnLabel(etl::istring& label) override {
		label.append(l10n::get(l10n::String::STRING_FOR_RELEASE_SHORT));
	}
};
class Ratio final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() override { return static_cast<uint64_t>(shown().ratio); }
	void setCompressorValue(q31_t value, ModControllableAudio& owner) override {
		effectSettingFor(owner, "audioCompressor/ratio", deluge::serialization::Value::makeNumber(value));
	}
	float getDisplayValue() override { return deluge::dsp::fixed::compressor_ratio(shown().ratio); }
	const char* getUnit() override { return " : 1"; }
};
class SideHPF final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() override { return static_cast<uint64_t>(shown().sidechain_hpf); }
	void setCompressorValue(q31_t value, ModControllableAudio& owner) override {
		effectSettingFor(owner, "audioCompressor/compHPF", deluge::serialization::Value::makeNumber(value));
	}
	float getDisplayValue() override { return deluge::dsp::fixed::compressor_sidechain_hz(shown().sidechain_hpf); }
	const char* getUnit() override { return "HZ"; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return HPF; }
};
class Blend final : public CompressorValue {
public:
	using CompressorValue::CompressorValue;
	uint64_t getCompressorValue() final { return static_cast<uint64_t>(shown().blend); }
	void setCompressorValue(q31_t, ModControllableAudio& owner) final {
		auto value = this->getValue();

		q31_t knobPos;
		if (value < kMaxKnobPos) {
			knobPos = lshiftAndSaturate<24>(value);
		}
		else {
			knobPos = ONE_Q31;
		}
		effectSettingFor(owner, "audioCompressor/compBlend", deluge::serialization::Value::makeNumber(knobPos));
	}

	float getDisplayValue() override { return deluge::dsp::fixed::compressor_blend_percent(shown().blend); }
	const char* getUnit() override { return " %"; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] int32_t getOccupiedSlots() const override { return 1; }
};
} // namespace deluge::gui::menu_item::audio_compressor
