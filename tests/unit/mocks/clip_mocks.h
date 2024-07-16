#pragma once

#include "definitions_cxx.hpp"

#include <cstdint>
#include <vector>

class Clip {
public:
	Clip(int id_ = -1) : id(id_) {}
	int id;
	ClipType type = ClipType::INSTRUMENT;
	bool deleted = false;
};

class InstrumentClip : public Clip {};

class AudioClip : public Clip {};

class ClipArray {
public:
	Clip** getElementAddress(int32_t index) { return &data[index]; }
	Clip* getClipAtIndex(int32_t index) { return data[index]; }
	int32_t getNumElements() { return data.size(); }
	void deleteAtIndex(int32_t index) {
		for (int32_t i = index; i < data.size() - 1; i++) {
			data[i] = data[i + 1];
		}
		data.pop_back();
	}
	void clear() { data.clear(); }
	void push(Clip* clip) { data.push_back(clip); }

private:
	std::vector<Clip*> data;
};
