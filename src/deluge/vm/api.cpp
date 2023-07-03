#include "api.h"
#include "../hid/buttons.h"
#include "../../definitions.h"
#include "../../RZA1/cpu_specific.h"


const char* WrenAPI::mainModuleSource =
#include "setup.wren.inc"
;
#include "buttons.h"

ModuleMap WrenAPI::modules() {
	static const ModuleMap map_ = {
		{"main", {
			{"TDeluge", {
				{"print(_)", {false,
					[](WrenVM* vm) -> void {
						const char* str = wrenGetSlotString(vm, 1);
						Wren::print(str);
					}
				}},
				{"pressButton(_,_,_)", {false,
					[](WrenVM* vm) -> void {
						wrenEnsureSlots(vm, 4);
						int x = (int)wrenGetSlotDouble(vm, 1);
						int y = (int)wrenGetSlotDouble(vm, 2);
						int z = (int)wrenGetSlotBool(vm, 3);
						Buttons::buttonAction(x, y, z, false);
					}
				}},
				/*
				{"mode_(_)", {false,
					[](WrenVM* vm) -> void {
						const char* str = wrenGetSlotString(vm, 1);
						Wren::print(str);
					}
				}},
				*/
			}},
		}},
	};
	return map_;
}



/*
// Looks for a built-in module with [name].
//
// Returns the BuildInModule for it or NULL if not found.
static ModuleRegistry* findModule(const char* name) {
  for (int i = 0; modules[i].name != NULL; i++)
  {
    if (strcmp(name, modules[i].name) == 0) return &modules[i];
  }

  return NULL;
}

// Looks for a class with [name] in [module].
static ClassRegistry* findClass(ModuleRegistry* module, const char* name) {
  for (int i = 0; module->classes[i].name != NULL; i++)
  {
    if (strcmp(name, module->classes[i].name) == 0) return &module->classes[i];
  }

  return NULL;
}

// Looks for a method with [signature] in [clas].
static WrenForeignMethodFn findMethod(ClassRegistry* clas, bool isStatic, const char* signature) {
  for (int i = 0; clas->methods[i].signature != NULL; i++)
  {
    MethodRegistry* method = &clas->methods[i];
    if (isStatic == method->isStatic &&
        strcmp(signature, method->signature) == 0)
    {
      return method->method;
    }
  }

  return NULL;
}

void loadModuleComplete(WrenVM* vm, const char* name, struct WrenLoadModuleResult result) {
  if (result.source == NULL) return;
}

WrenLoadModuleResult loadBuiltInModule(const char* name) {
  WrenLoadModuleResult result = {0};
  ModuleRegistry* module = findModule(name);
  if (module == NULL) return result;

  result.onComplete = loadModuleComplete;
  result.source = *module->source;
  return result;
}

WrenForeignMethodFn bindBuiltInForeignMethod(WrenVM* vm, const char* moduleName, const char* className, bool isStatic, const char* signature) {
  ModuleRegistry* module = findModule(moduleName);
  if (module == NULL) return NULL;

  ClassRegistry* clas = findClass(module, className);
  if (clas == NULL) return NULL;

  return findMethod(clas, isStatic, signature);
}

WrenForeignClassMethods bindBuiltInForeignClass(WrenVM* vm, const char* moduleName, const char* className) {
  WrenForeignClassMethods methods = { NULL, NULL };

  ModuleRegistry* module = findModule(moduleName);
if (module == NULL) return methods;

  ClassRegistry* clas = findClass(module, className);
  if (clas == NULL) return methods;

  methods.allocate = findMethod(clas, true, "<allocate>");
  methods.finalize = (WrenFinalizerFn)findMethod(clas, true, "<finalize>");

  return methods;
}
*/
