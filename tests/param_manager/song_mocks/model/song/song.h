#pragma once
#include "modulation/params/param_manager.h"
#include "util/container/array/resizeable_array.h"
#include <algorithm>
#include <cstdlib>
#include <vector>

class Clip {};
class ModControllableAudio;
struct BackedUpParamManager {
	ModControllableAudio* modControllable;
	Clip* clip;
	ParamManager paramManager;
};

class BackupTable {
public:
	std::vector<void*> entries;
	bool failInsertion = false;
	~BackupTable() {
		for (void* entry : entries) {
			static_cast<BackedUpParamManager*>(entry)->~BackedUpParamManager();
			std::free(entry);
		}
	}
	int getNumElements() const { return entries.size(); }
	void* getElementAddress(int index) { return entries.at(index); }
	static uint32_t key(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }
	int search(uint32_t owner, int comparison, int begin = 0, int end = -1) {
		if (end < 0) {
			end = getNumElements();
		}
		while (begin < end && key(static_cast<BackedUpParamManager*>(entries[begin])->modControllable) < owner) {
			++begin;
		}
		return begin;
	}
	int searchMultiWordExact(uint32_t* keys, int* insertion = nullptr) {
		int index = 0;
		for (; index < getNumElements(); ++index) {
			auto* entry = static_cast<BackedUpParamManager*>(entries[index]);
			auto owner = key(entry->modControllable);
			auto clip = key(entry->clip);
			if (owner == keys[0] && clip == keys[1]) {
				return index;
			}
			if (owner > keys[0] || (owner == keys[0] && clip > keys[1])) {
				break;
			}
		}
		if (insertion) {
			*insertion = index;
		}
		return -1;
	}
	Error insertAtIndex(int index) {
		if (failInsertion) {
			return Error::INSUFFICIENT_RAM;
		}
		void* memory = std::malloc(sizeof(BackedUpParamManager));
		if (!memory) {
			return Error::INSUFFICIENT_RAM;
		}
		entries.insert(entries.begin() + index, memory);
		return Error::NONE;
	}
	void deleteAtIndex(int index) {
		std::free(entries.at(index));
		entries.erase(entries.begin() + index);
	}
};

class Song {
public:
	BackupTable backedUpParamManagers;
	void backUpParamManager(ModControllableAudio*, Clip*, ParamManagerForTimeline*, bool);
	ParamManager* getBackedUpParamManagerForExactClip(ModControllableAudio*, Clip*, ParamManager* = nullptr);
	ParamManager* getBackedUpParamManagerPreferablyWithClip(ModControllableAudio*, Clip*, ParamManager* = nullptr);
	void deleteBackedUpParamManagersForClip(Clip*);
	void deleteBackedUpParamManagersForModControllable(ModControllableAudio*);
};
