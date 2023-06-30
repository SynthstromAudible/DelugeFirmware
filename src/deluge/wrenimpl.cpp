#include "hid/display/numeric_driver.h"
#include "storage/storage_manager.h"
#include "memory/general_memory_allocator.h"
#include "wrenimpl.h"
#include "memory/wren_heap.h"
#include <string>
#include "string.h"

static char scriptBuffer[SCRIPT_BUFFER_SIZE];

void Wren::writeFn(WrenVM* vm, const char* text) {
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

void Wren::errorFn(WrenVM* vm, WrenErrorType errorType, const char* mod, const int line, const char* msg) {
	switch (errorType) {
	case WREN_ERROR_COMPILE: {
		//printf("[%s line %d] [Error] %s\n", mod, line, msg);
	} break;
	case WREN_ERROR_STACK_TRACE: {
		//printf("[%s line %d] in %s\n", mod, line, msg);
	} break;
	case WREN_ERROR_RUNTIME: {
		//printf("[Runtime Error] %s\n", msg);
	} break;
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

Wren::Wren() {
	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.writeFn = &Wren::writeFn;
	config.errorFn = &Wren::errorFn;
	config.loadModuleFn = &Wren::loadModuleFn;
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
		runInit();
		first_run = false;
	}
}

#define NL "\n"
void Wren::setup() {
	static const char* setupScript =
		NL "class TDeluge {"
		NL "  construct new() { _init = Fn.new {} }"
		NL "  init() { _init.call() }"
		NL "  init=(val) { _init = val }"
		NL "}"
		NL "var Deluge = TDeluge.new()"
		NL;
	(void)interpret("main", setupScript);

	char* source = getSourceForModule("init");
	(void)wrenInterpret(vm, "main", source);
}

void Wren::runInit() {
	if (handles.Deluge != NULL and handles.init != NULL) {
		wrenSetSlotHandle(vm, 0, handles.Deluge);
		(void)wrenCall(vm, handles.init);
	}
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
