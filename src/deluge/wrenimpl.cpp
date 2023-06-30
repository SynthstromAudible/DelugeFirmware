#include "hid/display/numeric_driver.h"
#include "storage/storage_manager.h"
#include "memory/general_memory_allocator.h"
#include "wrenimpl.h"
#include "memory/wren_heap.h"
#include <string>

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

Wren::Wren() {
	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.writeFn = &Wren::writeFn;
	config.errorFn = &Wren::errorFn;
	config.reallocateFn = &wren_heap_realloc;
	config.initialHeapSize = kWrenHeapSize;
	config.minHeapSize = 4096;

	wren_heap_init();

	vm = wrenNewVM(&config);
	loadInitScript();
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

void Wren::loadInitScript() {
	FIL fil;
	UINT bytesRead = 0;
	const char* filename = "SCRIPTS/INIT.WREN";
	if (f_open(&fil, filename, FA_READ) != FR_OK) {
		return;
	}

	(void)f_read(&fil, &scriptBuffer, SCRIPT_BUFFER_SIZE, &bytesRead);
	f_close(&fil);
	scriptBuffer[bytesRead] = '\0';

	const char* mod = "main";
	(void)interpret(mod, scriptBuffer);
}

void Wren::runInit() {
	if (handles.Deluge != NULL and handles.init != NULL) {
		wrenSetSlotHandle(vm, 0, handles.Deluge);
		(void)wrenCall(vm, handles.init);
	}
}

void Wren::setupHandles() {
	handles = {NULL, NULL};
	if (!wrenHasModule(vm, "main")) return;

	handles.init = wrenMakeCallHandle(vm, "init()");

	if (wrenHasVariable(vm, "main", "Deluge")) {
		wrenEnsureSlots(vm, 1);
		wrenGetVariable(vm, "main", "Deluge", 0);
		handles.Deluge = wrenGetSlotHandle(vm, 0);
	}
}

void Wren::releaseHandles() {
	wrenReleaseHandle(vm, handles.init);
}
