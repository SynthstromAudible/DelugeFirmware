#include "api.h"
#include "hid/buttons.h"
#include "definitions.h"
#include "RZA1/cpu_specific.h"

const char* WrenAPI::mainModuleSource =
#include "main.wren.inc"
    ;
#include "buttons.h"

ButtonIndex WrenAPI::findButton(uint8_t x, uint8_t y) {
	for (int i = 1; i < ButtonIndex::maxButtonIndex; i++) {
		if (buttonValues[i].x == x and buttonValues[i].y == y) {
			return (ButtonIndex)i;
		}
	}
	return ButtonIndex::none;
}

// clang-format off
ModuleMap WrenAPI::modules() {
	static const ModuleMap map_ = {
		{"main", {
			{"TDeluge", {
				{"print(_)", {false,
					[](WrenVM* vm) -> void {
						const char* str = wrenGetSlotString(vm, 1);
						Wren::print(str);
					}
				}},
				{"pressButton(_,_)", {false,
					[](WrenVM* vm) -> void {
						wrenEnsureSlots(vm, 3);
						struct button_s* button = (struct button_s*) wrenGetSlotForeign(vm, 1);
						//int index = (int) wrenGetSlotDouble(vm, 1);
						//const struct button_s* button = &WrenAPI::buttonValues[index];
						bool down = (bool)wrenGetSlotBool(vm, 2);
						Buttons::buttonActionNoRe(button->x, button->y, down, false);
					}
				}},
			}},
			{"Button", {
				{"index", {false,
					[](WrenVM* vm) -> void {
						wrenEnsureSlots(vm, 1);
						struct button_s* button = (struct button_s*) wrenGetSlotForeign(vm, 0);
						ButtonIndex index = findButton(button->x, button->y);
						wrenSetSlotDouble(vm, 0, (int)index);
					}
				}},
			}},
		}},
	};
	return map_;
}
// clang-format on


