#pragma once

#include <map>
#include <string>
#include "hid/buttons.h"
#include "wren.hpp"
#include "wrenimpl.h"
#include "util/string.h"
#include "util/containers.h"

namespace Wren::API {
enum ButtonIndex {
	none = 0,
	affectEntire,
	song,
	clip,
	synth,
	kit,
	midi,
	cv,
	keyboard,
	scale,
	crossScreen,
	back,
	load,
	save,
	learn,
	tapTempo,
	syncScaling,
	triplets,
	play,
	record,
	shift,
	maxButtonIndex,
};

struct Method {
	bool isStatic;
	WrenForeignMethodFn fn;
};
using MethodMap = deluge::map<deluge::string, Method>;
using ClassMap = deluge::map<deluge::string, MethodMap>;
using ModuleMap = deluge::map<deluge::string, ClassMap>;

ModuleMap modules();
ButtonIndex findButton(hid::Button x);
extern const char* mainModuleSource;
extern const char* buttonsSource;
extern const hid::Button buttonValues[];
} // namespace Wren::API
