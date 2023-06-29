#include "hid/display/numeric_driver.h"
#include "memory/general_memory_allocator.h"
#include "wrenimpl.h"
#include "memory/wren_heap.h"

/*
extern "C" {
#include "wren/vm/wren_core.h"
#include "wren/vm/wren_vm.h"
#include "wren/vm/wren_utils.h"
}
*/

static void writeFn(WrenVM* vm, const char* text) {
	numericDriver.displayPopup(text);
}

static void errorFn(WrenVM* vm, WrenErrorType errorType, const char* mod, const int line, const char* msg) {
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

WrenVM* vm;

WrenVM* setupWren() {
	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.writeFn = &writeFn;
	config.errorFn = &errorFn;
	config.reallocateFn = &wren_heap_realloc;
	config.initialHeapSize = kWrenHeapSize;
	config.minHeapSize = 4096;

	wren_heap_init();

	vm = wrenNewVM(&config);
	return vm;

	/*
	memset(&vm, 0, sizeof(WrenVM));

	vm.config.writeFn = &writeFn;
	vm.config.errorFn = &errorFn;
	vm.config.reallocateFn = &reallocateFn;
	vm.config.initialHeapSize = 1024 * 1024;
	vm.config.minHeapSize = 1024 * 1024;

	vm.config.resolveModuleFn = NULL;
	vm.config.loadModuleFn = NULL;
	vm.config.bindForeignMethodFn = NULL;
	vm.config.bindForeignClassFn = NULL;
	vm.config.heapGrowthPercent = 50;
	vm.config.userData = NULL;

	vm.grayCount = 0;
	vm.grayCapacity = 4;
	vm.gray = (Obj**)vm.config.reallocateFn(NULL, vm.grayCapacity * sizeof(Obj*), NULL);
	vm.nextGC = vm.config.initialHeapSize;
	wrenSymbolTableInit(&vm.methodNames);
	vm.modules = wrenNewMap(&vm);
	wrenInitializeCore(&vm);
	*/
}
