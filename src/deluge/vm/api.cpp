#ifdef ENABLE_WREN

#include "api.h"
#include "hid/buttons.h"
#include "definitions.h"
#include "RZA1/cpu_specific.h"

namespace Wren::API {

const char* mainModuleSource =
#include "main.wren.inc"
    ;
#include "buttons.h"

ButtonIndex findButton(hid::Button b) {
	for (int i = 1; i < ButtonIndex::maxButtonIndex; i++) {
		if (buttonValues[i] == b) {
			return (ButtonIndex)i;
		}
	}
	return ButtonIndex::none;
}

namespace main {

namespace TDeluge {
void print(WrenVM* vm) {
	const char* str = wrenGetSlotString(vm, 1);
	VM::print(str);
}

void pressButton(WrenVM* vm) {
	wrenEnsureSlots(vm, 3);
	auto button = (hid::Button*)wrenGetSlotForeign(vm, 1);
	bool down = (bool)wrenGetSlotBool(vm, 2);
	Buttons::buttonActionNoRe(*button, down, false);
}
} //namespace TDeluge

namespace Button {
void index(WrenVM* vm) {
	wrenEnsureSlots(vm, 1);
	auto button = (hid::Button*)wrenGetSlotForeign(vm, 0);
	ButtonIndex index = findButton(*button);
	wrenSetSlotDouble(vm, 0, (int)index);
}
void allocate(WrenVM* vm) {
	auto data = (hid::Button*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(hid::Button));
	int index = (int)wrenGetSlotDouble(vm, 1);
	*data = API::buttonValues[index];
}
void finalize(WrenVM* data) {
}
} // namespace Button

} // namespace main

// clang-format off
ModuleMap modules() {
	using namespace main;
	static ModuleMap map_{};
	if (map_.empty()) {
		map_["main"] = {
			{"TDeluge", {
				{"print(_)",         {false, TDeluge::print}},
				{"pressButton(_,_)", {false, TDeluge::pressButton}},
			}},
			{"Button", {
				{"index",      {false, Button::index}},
				{"<allocate>", {false, Button::allocate}},
				{"<finalize>", {false, Button::finalize}},
			}},
		};
	}
	return map_;
}

}
// clang-format on

#endif
