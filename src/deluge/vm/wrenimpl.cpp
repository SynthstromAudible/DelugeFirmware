#include "hid/display/numeric_driver.h"
#include "storage/storage_manager.h"
#include "memory/general_memory_allocator.h"
#include "wrenimpl.h"
#include "api.h"
#include "memory/wren_heap.h"
#include <string>
#include "string.h"

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

void Wren::errorFn(WrenVM* vm, WrenErrorType errorType, const char* mod, const int line, const char* msg) {
	switch (errorType) {
	case WREN_ERROR_COMPILE:
		Wren::print("E compile");
		//printf("[%s line %d] [Error] %s\n", mod, line, msg);
		break;
	case WREN_ERROR_STACK_TRACE:
		Wren::print("E stacktrace");
		//printf("[%s line %d] in %s\n", mod, line, msg);
		break;
	case WREN_ERROR_RUNTIME:
		Wren::print("E runtime");
		//printf("[Runtime Error] %s\n", msg);
		break;
	default:
		Wren::print("E other");
		break;
	}
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

void Wren::loadModuleComplete(WrenVM* vm, const char *mod, WrenLoadModuleResult result) {
	// TODO
}

WrenForeignMethodFn Wren::bindForeignMethodFn( WrenVM* vm, const char* moduleName, const char* className, bool isStatic, const char* signature) {
	std::string mod(moduleName), cls(className), sig(signature);

	if (WrenAPI::modules().count(mod) > 0) {
		auto m = WrenAPI::modules()[mod];
		if (m.count(cls) > 0) {
			auto c = m[cls];
			if (c.count(sig) > 0) {
				auto s = c[sig];
				if (s.isStatic == isStatic) {
					return s.fn;
				} else {
					Wren::print("static?");
				}
			} else {
				Wren::print(signature);
			}
		} else {
			Wren::print(className);
		}
	} else {
		Wren::print(moduleName);
	}
	return [](WrenVM* vm) -> void {
	};
}

WrenForeignClassMethods Wren::bindForeignClassFn( WrenVM* vm, const char* moduleName, const char* className) {
	WrenForeignClassMethods methods;
	std::string mod(moduleName), cls(className);
	if (mod == "main") {
		if (cls == "File") {
			return {
				.allocate = [](WrenVM* vm) -> void {
					// TODO
				},
				.finalize = [](void* data) -> void {
					// TODO
				},
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
	handles = {NULL, NULL};

	wrenEnsureSlots(vm, 1);
	wrenGetVariable(vm, "main", "Deluge", 0);
	handles.Deluge = wrenGetSlotHandle(vm, 0);
	handles.init = wrenMakeCallHandle(vm, "init()");
}

void Wren::releaseHandles() {
	wrenReleaseHandle(vm, handles.init);
}
