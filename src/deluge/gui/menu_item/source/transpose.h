#pragma once
#include "gui/menu_item/decimal.h"
#include "gui/ui/sound_editor.h"
#include "gui/menu_item/transpose.h"

namespace menu_item::source {
class Transpose : public menu_item::Transpose {
public:
	Transpose(char const* newName = NULL, int newP = 0) : menu_item::Transpose(newName, newP) {}

	ParamDescriptor getLearningThing() final {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(getP());
		return paramDescriptor;
	}

protected:
	uint8_t getP() final { return p + soundEditor.currentSourceIndex; }
};
} // namespace menu_item
