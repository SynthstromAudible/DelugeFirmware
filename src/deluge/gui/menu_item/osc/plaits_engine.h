#pragma once
#include "gui/menu_item/patched_param/integer.h"
#include "gui/ui/sound_editor.h"
#include "modulation/params/param_set.h"
#include "processing/plaits/engine_parameter.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include <cmath>
#include <cstdio>
namespace deluge::gui::menu_item::osc {
class PlaitsEngine final : public patched_param::Integer {
	int oscillator;

public:
	explicit PlaitsEngine(int osc)
	    : patched_param::Integer(l10n::String::STRING_FOR_TYPE,
	                             deluge::modulation::params::LOCAL_PLAITS_A_ENGINE + osc),
	      oscillator(osc) {}
	void drawValue() override { display->setTextAsNumber(getDisplayValue()); }
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		char label[16];
		snprintf(label, sizeof(label), "%d %s", int(getDisplayValue()), getUnit());
		deluge::hid::display::OLED::main.drawStringCentered(label, slot.start_x, slot.start_y + 5, 6, 7, slot.width);
	}
	std::string_view getName() const override { return "Engine"; }
	std::string_view getTitle() const override { return "Plaits Engine"; }
	bool isRelevant(ModControllableAudio* mc, int32_t) const override {
		auto* sound = static_cast<Sound*>(mc);
		return sound->getSynthMode() == SynthMode::SUBTRACTIVE && isPlaits(sound->sources[oscillator].oscType);
	}
	void readCurrentValue() override {
		auto base = util::to_underlying(soundEditor.currentSound->sources[oscillator].oscType)
		            - util::to_underlying(OscType::PLAITS_FM);
		auto raw = soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP());
		setValue(25 * deluge::plaits_control::modelFromPreset(base, raw));
	}
	void selectEncoderAction(int32_t offset) override {
		patched_param::Integer::selectEncoderAction(std::clamp<int32_t>(offset, -2, 2) * 25);
	}
	const char* getUnit() override {
		static const char* models[] = {"FM", "Wave", "VA"};
		return models[std::clamp<int>(getValue() / 25, 0, 2)];
	}
	int32_t getDisplayValue() override { return getValue() / 25 + 1; }
	int32_t getFinalValue() override {
		int base = util::to_underlying(soundEditor.currentSound->sources[oscillator].oscType)
		           - util::to_underlying(OscType::PLAITS_FM);
		int64_t raw = (getValue() / 25 - base) * 1073741824LL;
		return std::clamp<int64_t>(raw, INT32_MIN, INT32_MAX);
	}
};
} // namespace deluge::gui::menu_item::osc
