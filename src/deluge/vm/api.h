#pragma once

#include "wrenimpl.h"
#include <map>
#include <string>

typedef struct {
	bool isStatic;
	WrenForeignMethodFn fn;
} Method;
typedef std::map<std::string, Method> MethodMap;
typedef std::map<std::string, MethodMap> ClassMap;
typedef std::map<std::string, ClassMap> ModuleMap;

class WrenAPI {
public:
	static ModuleMap modules();
	static const char* mainModuleSource;
	static const char* buttonsSource;
};
