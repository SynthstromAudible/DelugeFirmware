#pragma once

#include "util/container/array/ordered_resizeable_array_with_multi_word_key.h"
#include <array>
#include <bit>
#include <optional>

class AutoParamData;

// Scoped to instrument preset saving. Retain the values actually serialized,
// since automation can advance while file IO services the audio engine.
class PresetSaveValues {
public:
	PresetSaveValues() { active = this; }
	~PresetSaveValues() { active = previous_; }
	PresetSaveValues(const PresetSaveValues&) = delete;
	PresetSaveValues& operator=(const PresetSaveValues&) = delete;
	static void record(const AutoParamData* param, int32_t value) {
		if (active == nullptr || active->error_ != Error::NONE) {
			return;
		}
		auto key = keyFor(param);
		int32_t index = active->entries_.searchMultiWordExact(key.data());
		if (index < 0) {
			index = active->entries_.insertAtKeyMultiWord(key.data());
		}
		if (index < 0) {
			active->error_ = Error::INSUFFICIENT_RAM;
		}
		else {
			static_cast<Entry*>(active->entries_.getElementAddress(index))->value = value;
		}
	}
	std::optional<int32_t> find(const AutoParamData* param) {
		auto key = keyFor(param);
		int32_t index = entries_.searchMultiWordExact(key.data());
		if (index >= 0) {
			return static_cast<Entry*>(entries_.getElementAddress(index))->value;
		}
		return std::nullopt;
	}

	[[nodiscard]] Error error() const { return error_; }

private:
	// One address word on ARM, two in the 64-bit host tests.
	// NOLINTNEXTLINE(bugprone-sizeof-expression)
	static constexpr int32_t keyWords = sizeof(uintptr_t) / sizeof(uint32_t);
	using Key = std::array<uint32_t, keyWords>;
	static Key keyFor(const AutoParamData* param) {
		// Only compare pointer identity; never reconstruct or dereference it.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		return std::bit_cast<Key>(reinterpret_cast<uintptr_t>(param));
	}
	struct Entry {
		Key key;
		int32_t value;
	};
	inline static PresetSaveValues* active = nullptr;
	PresetSaveValues* previous_ = active;
	OrderedResizeableArrayWithMultiWordKey entries_{sizeof(Entry), keyWords, 0, 16};
	Error error_ = Error::NONE;
};
