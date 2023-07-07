#pragma once
#include "wren.hpp"
#include "hid/button.h"

#define SCRIPT_BUFFER_SIZE 1024

namespace Wren {

class VM {
public:
	bool first_run;
	WrenVM* vm;

	struct {
		WrenHandle *Deluge, *init, *Button, *buttonAction;
	} handles;

	VM();
	~VM();
	void setupHandles();
	void releaseHandles();
	void tick();
	void setup();
	void init();
	void buttonAction(hid::Button b, bool on);
	inline WrenInterpretResult interpret(const char*, const char*);

	static void print(const char* text);

protected:
	static void errorFn(WrenVM* vm, WrenErrorType errorType, const char* mod, const int line, const char* msg);
	static void writeFn(WrenVM* vm, const char* text);
	static WrenLoadModuleResult loadModuleFn(WrenVM* vm, const char* name);
	static void loadModuleComplete(WrenVM* vm, const char* mod, WrenLoadModuleResult result);
	static char* getSourceForModule(const char*);
	static WrenForeignMethodFn bindForeignMethodFn(WrenVM* vm, const char* module, const char* className, bool isStatic,
	                                               const char* signature);
	static WrenForeignClassMethods bindForeignClassFn(WrenVM* vm, const char* mod, const char* cls);
};

}
