#pragma once

#include "wrenimpl.h"
#include <map>
#include <string>

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

		struct button_s {
			uint8_t x, y;
		};

		typedef struct {
			bool isStatic;
			WrenForeignMethodFn fn;
		} Method;
		typedef std::map<std::string, Method> MethodMap;
		typedef std::map<std::string, MethodMap> ClassMap;
		typedef std::map<std::string, ClassMap> ModuleMap;

		ModuleMap modules();
		ButtonIndex findButton(uint8_t x, uint8_t y);
		extern const char* mainModuleSource;
		extern const char* buttonsSource;
		extern const button_s buttonValues[];
	}
}
