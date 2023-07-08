#include "hid/display/numeric_driver.h"
#include "storage/storage_manager.h"
#include "memory/general_memory_allocator.h"
#include "wrenimpl.h"
#include "api.h"
#include "memory/wren_heap.h"
#include "string.h"

extern "C" {
#include "drivers/uart/uart.h"
}

namespace Wren {

static char scriptBuffer[SCRIPT_BUFFER_SIZE];

void VM::print(const char* text) {
	bool empty = true;
	for (size_t i = 0; text[i] != '\0'; i++) {
		if (!std::isspace(static_cast<unsigned char>(text[i]))) {
			empty = false;
		}
	}
	if (empty)
		return;

#if HAVE_OLED
	numericDriver.displayPopup(text);
#else
	if (strlen(text) <= NUMERIC_DISPLAY_LENGTH) {
		numericDriver.setText(text, true);
	}
	else {
		numericDriver.setScrollingText(text);
	}
#endif
}

void VM::writeFn(WrenVM* vm, const char* text) {
	VM::print(text);
}

//#define WREN_LOG(x) len = strlen(x); if(len > 200) len = 200; f_write(&fil, x, len, &written);

void VM::errorFn(WrenVM* vm, WrenErrorType errorType, const char* mod, const int line, const char* msg) {
	//UINT written;
	//FIL fil;
	//auto res = storageManager.createFile(&fil, "SCRIPTS/log.txt", true);
	//size_t len;

	switch (errorType) {
	case WREN_ERROR_COMPILE:
		VM::print("E compile");

		uartPrint(mod);
		uartPrint(":");
		uartPrintNumberSameLine(line);
		uartPrint(" E compile ");
		uartPrintln(msg);
		break;
	case WREN_ERROR_STACK_TRACE:
		VM::print("E stacktrace");

		uartPrint(mod);
		uartPrint(":");
		uartPrintNumberSameLine(line);
		uartPrint(" E stacktrace ");
		uartPrintln(msg);

		break;
	case WREN_ERROR_RUNTIME:
		VM::print("E runtime");

		uartPrint("E runtime ");
		uartPrintln(msg);
		break;
	default:
		VM::print("E unknown");

		uartPrintln("E unknown");
		break;
	}
	//WREN_LOG("\n");
	//f_close(&fil);
}

void VM::buttonAction(hid::Button b, bool on) {
	API::ButtonIndex index = Wren::API::findButton(b);
	wrenEnsureSlots(vm, 3);
	wrenSetSlotHandle(vm, 0, handles.Deluge);
	wrenSetSlotDouble(vm, 1, index);
	wrenSetSlotBool(vm, 2, on);
	(void)wrenCall(vm, handles.buttonAction);
}

char* VM::getSourceForModule(const char* name) {
	FIL fil;
	UINT bytesRead = 0;
	TCHAR filename[256];
	sprintf(filename, "SCRIPTS/%s.wren", name);
	if (f_open(&fil, filename, FA_READ) == FR_OK) {
		(void)f_read(&fil, &scriptBuffer, SCRIPT_BUFFER_SIZE, &bytesRead);
		f_close(&fil);
	}
	scriptBuffer[bytesRead] = '\0';

	return scriptBuffer;
}

WrenLoadModuleResult VM::loadModuleFn(WrenVM* vm, const char* name) {
	WrenLoadModuleResult result = {0};
	char* source = getSourceForModule(name);
	result.source = source;
	result.onComplete = &VM::loadModuleComplete;
	return result;
}

void VM::loadModuleComplete(WrenVM* vm, const char* mod, WrenLoadModuleResult result) {
	// TODO
}

WrenForeignMethodFn VM::findModuleFunc(WrenVM* vm, std::string mod, std::string cls, bool isStatic, std::string sig) {
	if (Wren::API::modules().count(mod) > 0) {
		auto m = Wren::API::modules()[mod];
		if (m.count(cls) > 0) {
			auto c = m[cls];
			if (c.count(sig) > 0) {
				auto s = c[sig];
				if (s.isStatic == isStatic) {
					return s.fn;
				}
				else {
					VM::print("static?");
				}
			}
			else {
				VM::print(sig.c_str());
			}
		}
		else {
			VM::print(cls.c_str());
		}
	}
	else {
		VM::print(mod.c_str());
	}
	return [](WrenVM* vm) -> void {};
}

WrenForeignMethodFn VM::bindForeignMethodFn(WrenVM* vm, const char* moduleName, const char* className, bool isStatic,
                                            const char* signature) {
	std::string mod(moduleName), cls(className), sig(signature);
	return VM::findModuleFunc(vm, mod, cls, isStatic, sig);
}

WrenForeignClassMethods VM::bindForeignClassFn(WrenVM* vm, const char* moduleName, const char* className) {
	std::string mod(moduleName), cls(className);
	return {
	    .allocate = findModuleFunc(vm, mod, cls, false, "<allocate>"),
	    .finalize = (WrenFinalizerFn)findModuleFunc(vm, mod, cls, false, "<finalize>"),
	};
}

VM::VM() {
	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.writeFn = &VM::writeFn;
	config.errorFn = &VM::errorFn;
	config.loadModuleFn = &VM::loadModuleFn;
	config.bindForeignMethodFn = &VM::bindForeignMethodFn;
	config.bindForeignClassFn = &VM::bindForeignClassFn;
	config.reallocateFn = &wren_heap_realloc;
	config.initialHeapSize = kWrenHeapSize;
	config.minHeapSize = 4096;

	wren_heap_init();

	vm = wrenNewVM(&config);
	setup();
	setupHandles();
	first_run = true;
}

VM::~VM() {
	releaseHandles();
	wrenFreeVM(vm);
}

inline WrenInterpretResult VM::interpret(const char* mod, const char* source) {
	return wrenInterpret(vm, mod, source);
}

void VM::tick() {
	if (first_run) {
		init();
		first_run = false;
	}
}

void VM::setup() {
	(void)interpret("main", Wren::API::buttonsSource);
	(void)interpret("main", Wren::API::mainModuleSource);

	char* source = getSourceForModule("init");
	(void)interpret("main", source);
}

void VM::init() {
	wrenSetSlotHandle(vm, 0, handles.Deluge);
	(void)wrenCall(vm, handles.init);
}

void VM::setupHandles() {
	handles = {0};

	wrenEnsureSlots(vm, 1);

	wrenGetVariable(vm, "main", "Deluge", 0);
	handles.Deluge = wrenGetSlotHandle(vm, 0);
	handles.init = wrenMakeCallHandle(vm, "init()");

	wrenGetVariable(vm, "main", "Button", 0);
	handles.Button = wrenGetSlotHandle(vm, 0);
	handles.buttonAction = wrenMakeCallHandle(vm, "buttonAction(_,_)");
}

void VM::releaseHandles() {
	wrenReleaseHandle(vm, handles.init);
}

} // namespace Wren
