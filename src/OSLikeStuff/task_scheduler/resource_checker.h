//
// Created by mark on 18/01/25.
//

#ifndef RESOURCE_CHECKER_H
#define RESOURCE_CHECKER_H
#include <OSLikeStuff/scheduler_api.h>
#include <bitset>
#include <extern.h>
extern uint8_t currentlyAccessingCard;
extern uint32_t usbLock;
// this is basically a bitset however the enums need to be exposed to C code and this is easier to keep synced
class ResourceChecker {
	uint32_t resources_{0};

public:
	ResourceChecker() = default;
	ResourceChecker(ResourceID resources) : resources_(resources) {};
	/// returns whether all resources are available. This is basically shitty priority ceiling to avoid the potential of
	/// a task locking one resource and then trying to yield while it waits for another
	bool checkResources() const {
		if (resources_ == NO_RESOURCE) {
			return true;
		}
		bool anythingLocked = false;
		if ((resources_ & SD) != 0u) {
			anythingLocked |= currentlyAccessingCard;
		}
		if ((resources_ & USB) != 0u) {
			anythingLocked |= usbLock;
		}
		if ((resources_ & SD_ROUTINE) != 0u) {
			anythingLocked |= sdRoutineLock;
		}
		return !anythingLocked;
	}
};

#endif // RESOURCE_CHECKER_H
