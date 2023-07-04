#include "hid/display/numeric_driver.h"
#include "storage/storage_manager.h"
#include "memory/general_memory_allocator.h"
#include "wrenimpl.h"
#include "api.h"
#include "memory/wren_heap.h"
#include <string>
#include "string.h"

extern "C" {
#include "drivers/uart/uart.h"
}

static char scriptBuffer[SCRIPT_BUFFER_SIZE];

void Wren::print(const char* text) {
	bool empty = true;
	for (size_t i = 0; text[i] != '\0'; i++) {
		if (!std::isspace(static_cast<unsigned char>(text[i]))) {
			empty = false;
		}
	}
	if (empty) return;

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

void Wren::writeFn(WrenVM* vm, const char* text) {
	Wren::print(text);
}

//#define WREN_LOG(x) len = strlen(x); if(len > 200) len = 200; f_write(&fil, x, len, &written);

void Wren::errorFn(WrenVM* vm, WrenErrorType errorType, const char* mod, const int line, const char* msg) {
	//UINT written;
	//FIL fil;
	//auto res = storageManager.createFile(&fil, "SCRIPTS/log.txt", true);
	//size_t len;

	switch (errorType) {
	case WREN_ERROR_COMPILE:
		Wren::print("E compile");

		uartPrint(mod);
		uartPrint(":");
		uartPrintNumberSameLine(line);
		uartPrint(" E compile ");
		uartPrintln(msg);
		break;
	case WREN_ERROR_STACK_TRACE:
		Wren::print("E stacktrace");

		uartPrint(mod);
		uartPrint(":");
		uartPrintNumberSameLine(line);
		uartPrint(" E stacktrace ");
		uartPrintln(msg);

		break;
	case WREN_ERROR_RUNTIME:
		Wren::print("E runtime");

		uartPrint("E runtime ");
		uartPrintln(msg);
		break;
	default:
		Wren::print("E unknown");

		uartPrintln("E unknown");
		break;
	}
	//WREN_LOG("\n");
	//f_close(&fil);
}

void Wren::buttonAction(int x, int y, bool on) {
	ButtonIndex index = WrenAPI::findButton(x, y);
	wrenEnsureSlots(vm, 3);
	wrenSetSlotHandle(vm, 0, handles.Deluge);
	wrenSetSlotDouble(vm, 1, index);
	wrenSetSlotBool(vm, 2, on);
	(void)wrenCall(vm, handles.buttonAction);
}

char* Wren::getSourceForModule(const char* name) {
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

WrenLoadModuleResult Wren::loadModuleFn(WrenVM* vm, const char* name) {
	WrenLoadModuleResult result = {0};
	char* source = getSourceForModule(name);
	result.source = source;
	result.onComplete = &Wren::loadModuleComplete;
	return result;
}

void Wren::loadModuleComplete(WrenVM* vm, const char* mod, WrenLoadModuleResult result) {
	// TODO
}

WrenForeignMethodFn Wren::bindForeignMethodFn(WrenVM* vm, const char* moduleName, const char* className, bool isStatic,
                                              const char* signature) {
	std::string mod(moduleName), cls(className), sig(signature);

	if (WrenAPI::modules().count(mod) > 0) {
		auto m = WrenAPI::modules()[mod];
		if (m.count(cls) > 0) {
			auto c = m[cls];
			if (c.count(sig) > 0) {
				auto s = c[sig];
				if (s.isStatic == isStatic) {
					return s.fn;
				}
				else {
					Wren::print("static?");
				}
			}
			else {
				Wren::print(signature);
			}
		}
		else {
			Wren::print(className);
		}
	}
	else {
		Wren::print(moduleName);
	}
	return [](WrenVM* vm) -> void {};
}

WrenForeignClassMethods Wren::bindForeignClassFn(WrenVM* vm, const char* moduleName, const char* className) {
	WrenForeignClassMethods methods;
	std::string mod(moduleName), cls(className);
	if (mod == "main") {
		if (cls == "Button") {
			return {
			    .allocate = [](WrenVM* vm) -> void {
				    button_s* data = (button_s*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(button_s));
				    int index = (int)wrenGetSlotDouble(vm, 1);
				    *data = WrenAPI::buttonValues[index];
			    },
			    .finalize = [](void* data) -> void { data = NULL; },
			};
		}
	}
	return {
	    .allocate = [](WrenVM* vm) -> void {},
	    .finalize = [](void* data) -> void {},
	};
}

Wren::Wren() {
	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.writeFn = &Wren::writeFn;
	config.errorFn = &Wren::errorFn;
	config.loadModuleFn = &Wren::loadModuleFn;
	config.bindForeignMethodFn = &Wren::bindForeignMethodFn;
	config.bindForeignClassFn = &Wren::bindForeignClassFn;
	config.reallocateFn = &wren_heap_realloc;
	config.initialHeapSize = kWrenHeapSize;
	config.minHeapSize = 4096;

	wren_heap_init();

	vm = wrenNewVM(&config);
	setup();
	setupHandles();
	first_run = true;
}

Wren::~Wren() {
	releaseHandles();
	wrenFreeVM(vm);
}

inline WrenInterpretResult Wren::interpret(const char* mod, const char* source) {
	return wrenInterpret(vm, mod, source);
}

void Wren::tick() {
	if (first_run) {
		init();
		first_run = false;
	}
}

void Wren::setup() {
	(void)interpret("main", WrenAPI::buttonsSource);
	(void)interpret("main", WrenAPI::mainModuleSource);

	char* source = getSourceForModule("init");
	(void)interpret("main", source);
}

void Wren::init() {
	wrenSetSlotHandle(vm, 0, handles.Deluge);
	(void)wrenCall(vm, handles.init);
}

void Wren::setupHandles() {
	handles = {0};

	wrenEnsureSlots(vm, 1);

	wrenGetVariable(vm, "main", "Deluge", 0);
	handles.Deluge = wrenGetSlotHandle(vm, 0);
	handles.init = wrenMakeCallHandle(vm, "init()");

	wrenGetVariable(vm, "main", "Button", 0);
	handles.Button = wrenGetSlotHandle(vm, 0);
	handles.buttonAction = wrenMakeCallHandle(vm, "buttonAction(_,_)");
}

void Wren::releaseHandles() {
	wrenReleaseHandle(vm, handles.init);
}
