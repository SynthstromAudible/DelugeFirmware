#pragma once

#include "util/container/array/ordered_resizeable_array_with_multi_word_key.h"
#include <algorithm>
#include <array>
#include <bit>
#include <optional>

class ParamCollection;

// Scoped to instrument preset saving. Retain the values actually serialized,
// since automation can advance while file IO services the audio engine.
// Entries are keyed by collection and parameter ID: file IO runs the audio and MIDI
// routines, which can re-sort patch cables or relocate MIDI CC entries mid-save.
class PresetSaveValues {
public:
	PresetSaveValues() { active = this; }
	~PresetSaveValues() { active = previous_; }
	PresetSaveValues(const PresetSaveValues&) = delete;
	PresetSaveValues& operator=(const PresetSaveValues&) = delete;
	static void record(const ParamCollection* collection, int32_t id, int32_t value) {
		if (active == nullptr || active->error_ != Error::NONE) {
			return;
		}
		auto key = keyFor(collection, id);
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
	std::optional<int32_t> find(const ParamCollection* collection, int32_t id) {
		auto key = keyFor(collection, id);
		int32_t index = entries_.searchMultiWordExact(key.data());
		if (index >= 0) {
			return static_cast<Entry*>(entries_.getElementAddress(index))->value;
		}
		return std::nullopt;
	}

	[[nodiscard]] Error error() const { return error_; }

private:
	// The collection address (one word on ARM, two in the 64-bit host tests), then the ID.
	// NOLINTNEXTLINE(bugprone-sizeof-expression)
	static constexpr int32_t addressWords = sizeof(uintptr_t) / sizeof(uint32_t);
	static constexpr int32_t keyWords = addressWords + 1;
	using Key = std::array<uint32_t, keyWords>;
	static Key keyFor(const ParamCollection* collection, int32_t id) {
		// Only compare pointer identity; never reconstruct or dereference it.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		auto address = std::bit_cast<std::array<uint32_t, addressWords>>(reinterpret_cast<uintptr_t>(collection));
		Key key{};
		std::ranges::copy(address, key.begin());
		key.back() = static_cast<uint32_t>(id);
		return key;
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
