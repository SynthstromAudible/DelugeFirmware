#pragma once
#include "gui/menu_item/mod_fx/type.h"

namespace menu_item::audio_clip::mod_fx {
class Type final : public menu_item::mod_fx::Type {
public:
	using menu_item::mod_fx::Type::Type;

	// We override this to set min value to 1. We don't inherit any getMinValue() function to override more easily
	void selectEncoderAction(int offset) override {
		soundEditor.currentValue += offset;
		int numOptions = getNumOptions();

		if (soundEditor.currentValue >= numOptions) soundEditor.currentValue -= (numOptions - 1);
		else if (soundEditor.currentValue < 1) soundEditor.currentValue += (numOptions - 1);

		Value::selectEncoderAction(offset);
	}
};
} // namespace menu_item::audio_clip::mod_fx
