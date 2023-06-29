#pragma once
#include "wren.hpp"

#define SCRIPT_BUFFER_SIZE 1024

class Wren {
public:
	bool first_run;
	WrenVM* vm;

	Wren();
	void tick();
	void autoexec();
	inline WrenInterpretResult interpret(const char*, const char*);

private:
	static void errorFn(WrenVM* vm, WrenErrorType errorType, const char* mod, const int line, const char* msg);
	static void writeFn(WrenVM* vm, const char* text);
	char scriptBuffer[SCRIPT_BUFFER_SIZE];
};
