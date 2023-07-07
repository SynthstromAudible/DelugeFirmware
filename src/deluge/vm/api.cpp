#include "api.h"
#include "hid/buttons.h"
#include "definitions.h"
#include "RZA1/cpu_specific.h"

namespace Wren {
namespace API {

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

// clang-format off
ModuleMap modules() {
	static const ModuleMap map_ = {
		{"main", {
			{"TDeluge", {
				{"print(_)", {false,
					[](WrenVM* vm) -> void {
						const char* str = wrenGetSlotString(vm, 1);
						VM::print(str);
					}
				}},
				{"pressButton(_,_)", {false,
					[](WrenVM* vm) -> void {
						wrenEnsureSlots(vm, 3);
						auto button = (hid::Button*) wrenGetSlotForeign(vm, 1);
						//int index = (int) wrenGetSlotDouble(vm, 1);
						//const struct button_s* button = &WrenAPI::buttonValues[index];
						bool down = (bool)wrenGetSlotBool(vm, 2);
						Buttons::buttonActionNoRe(*button, down, false);
					}
				}},
			}},
			{"Button", {
				{"index", {false,
					[](WrenVM* vm) -> void {
						wrenEnsureSlots(vm, 1);
						auto button = (hid::Button*) wrenGetSlotForeign(vm, 0);
						ButtonIndex index = findButton(*button);
						wrenSetSlotDouble(vm, 0, (int)index);
					}
				}},
			}},
		}},
	};
	return map_;
}

}}
// clang-format on
