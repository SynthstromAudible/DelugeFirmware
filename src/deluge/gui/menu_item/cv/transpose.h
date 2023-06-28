#pragma once
#include "processing/engines/cv_engine.h"
#include "gui/menu_item/decimal.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::cv {
class Transpose final : public Decimal {
public:
	Transpose(char const* newName = NULL) : Decimal(newName) {
#if HAVE_OLED
		static char cvTransposeTitle[] = "CVx transpose";
		basicTitle = cvTransposeTitle;
#endif
	}
	int getMinValue() const { return -9600; }
	int getMaxValue() const { return 9600; }
	int getNumDecimalPlaces() const { return 2; }
	void readCurrentValue() {
		soundEditor.currentValue = (int32_t)cvEngine.cvChannels[soundEditor.currentSourceIndex].transpose * 100
		                           + cvEngine.cvChannels[soundEditor.currentSourceIndex].cents;
	}
	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;
		cvEngine.setCVTranspose(soundEditor.currentSourceIndex, semitones - 256, cents);
	}
};

} // namespace menu_item::cv
