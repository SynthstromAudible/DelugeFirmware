#pragma once

#include <map>
#include <string>
#include "hid/buttons.h"
#include "wrenimpl.h"

namespace Wren {
namespace API {
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

typedef struct {
	bool isStatic;
	WrenForeignMethodFn fn;
} Method;
typedef std::map<std::string, Method> MethodMap;
typedef std::map<std::string, MethodMap> ClassMap;
typedef std::map<std::string, ClassMap> ModuleMap;

ModuleMap modules();
ButtonIndex findButton(hid::Button x);
extern const char* mainModuleSource;
extern const char* buttonsSource;
extern const hid::Button buttonValues[];
} // namespace API
} // namespace Wren
