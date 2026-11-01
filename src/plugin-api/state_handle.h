/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief A plugin's state, owned and held without the holder knowing its type: what a project record keeps of the
/// instrument a track or kit row plays.

#include "plugin-api/plugin.h"
#include "serialization/value.h"

#include <concepts>
#include <expected>
#include <utility>

namespace deluge::plugin {

/// @brief A plugin's state, behind one allocation: copying copies the state, moving moves the allocation (so a pointer
/// into the state stays valid), and equality compares the states. Only the plugin's own code, which names its type, can
/// reach the state (as()).
class StateHandle {
public:
	/// @brief An empty handle: no plugin, no state.
	StateHandle() = default;

	/// @brief A handle owning @p state, of plugin @p E.
	/// @tparam E The plugin; its State must be equality-comparable.
	/// @param state The state.
	/// @return The handle.
	template <InstrumentPlugin E>
	requires std::equality_comparable<typename E::State>
	[[nodiscard]] static StateHandle of(typename E::State state) {
		StateHandle handle;
		handle.ops_ = &kOps<E>;
		handle.state_ = new typename E::State(std::move(state));
		return handle;
	}

	StateHandle(const StateHandle& other)
	    : ops_(other.ops_), state_(other.ops_ ? other.ops_->clone(other.state_) : nullptr) {}
	StateHandle& operator=(const StateHandle& other) {
		if (this != &other) {
			StateHandle copy(other);
			swap(copy);
		}
		return *this;
	}
	StateHandle(StateHandle&& other) noexcept
	    : ops_(std::exchange(other.ops_, nullptr)), state_(std::exchange(other.state_, nullptr)) {}
	StateHandle& operator=(StateHandle&& other) noexcept {
		StateHandle moved(std::move(other));
		swap(moved);
		return *this;
	}
	~StateHandle() {
		if (ops_ != nullptr) {
			ops_->destroy(state_);
		}
	}

	/// @brief Whether the handle holds a state.
	[[nodiscard]] bool has_value() const { return ops_ != nullptr; }
	/// @brief The id of the plugin whose state this is; empty for an empty handle.
	[[nodiscard]] PluginId plugin() const { return ops_ != nullptr ? ops_->id : PluginId{}; }

	/// @brief The state as its plugin writes it, for a file or an inspection.
	/// @param context The file context.
	/// @return The plugin's encoding; an empty Map for an empty handle.
	[[nodiscard]] serialization::Value encode(const CodecContext& context) const {
		return ops_ != nullptr ? ops_->encode(state_, context) : serialization::Value::makeMap();
	}
	/// @brief Read @p value into the state, as its plugin reads it; what @p value lacks keeps its value.
	/// @param value   The plugin's encoding.
	/// @param context The file context.
	/// @return Nothing, or why it cannot be read (an empty handle reads nothing: NOT_A_MAP).
	[[nodiscard]] std::expected<void, StateError> decode(const serialization::Value& value,
	                                                     const CodecContext& context) {
		if (ops_ == nullptr) {
			return std::unexpected(StateError::NOT_A_MAP);
		}
		return ops_->decode(state_, value, context);
	}

	/// @brief The state, for plugin @p E's own code.
	/// @tparam E The plugin.
	/// @return The state, or nullptr for an empty handle or another plugin's state.
	template <InstrumentPlugin E>
	[[nodiscard]] typename E::State* as() {
		return ops_ == &kOps<E> ? static_cast<typename E::State*>(state_) : nullptr;
	}
	/// @copydoc as()
	template <InstrumentPlugin E>
	[[nodiscard]] const typename E::State* as() const {
		return ops_ == &kOps<E> ? static_cast<const typename E::State*>(state_) : nullptr;
	}

	/// @brief Equal when both are empty, or both hold states of one plugin that compare equal.
	bool operator==(const StateHandle& other) const {
		if (ops_ != other.ops_) {
			return false;
		}
		return ops_ == nullptr || ops_->equal(state_, other.state_);
	}

private:
	/// What the handle does with a state it cannot name: one table per plugin type.
	struct Ops {
		PluginId id;
		void* (*clone)(const void*);
		void (*destroy)(void*);
		bool (*equal)(const void*, const void*);
		serialization::Value (*encode)(const void*, const CodecContext&);
		std::expected<void, StateError> (*decode)(void*, const serialization::Value&, const CodecContext&);
	};

	template <InstrumentPlugin E>
	static constexpr Ops kOps{
	    .id = E::descriptor().id,
	    .clone = [](const void* s) -> void* {
		    return new typename E::State(*static_cast<const typename E::State*>(s));
	    },
	    .destroy = [](void* s) { delete static_cast<typename E::State*>(s); },
	    .equal =
	        [](const void* a, const void* b) {
		        return *static_cast<const typename E::State*>(a) == *static_cast<const typename E::State*>(b);
	        },
	    .encode =
	        [](const void* s, const CodecContext& context) {
		        return E::encode(*static_cast<const typename E::State*>(s), context);
	        },
	    .decode =
	        [](void* s, const serialization::Value& value, const CodecContext& context) {
		        return E::decode(value, *static_cast<typename E::State*>(s), context);
	        },
	};

	void swap(StateHandle& other) noexcept {
		std::swap(ops_, other.ops_);
		std::swap(state_, other.state_);
	}

	const Ops* ops_{nullptr};
	void* state_{nullptr};
};

} // namespace deluge::plugin
