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
	Clip*& operator[](size_t index) { return data[index]; }
	Clip* const& operator[](size_t index) const { return data[index]; }
	[[nodiscard]] size_t size() const { return data.size(); }
	[[nodiscard]] bool empty() const { return data.empty(); }
	auto begin() { return data.begin(); }
	auto end() { return data.end(); }
	auto erase(std::vector<Clip*>::iterator at) { return data.erase(at); }
	void clear() { data.clear(); }
	void push(Clip* clip) { data.push_back(clip); }

private:
	std::vector<Clip*> data;
};
