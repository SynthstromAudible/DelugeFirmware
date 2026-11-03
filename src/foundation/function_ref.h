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

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace deluge {

template <class Signature>
class function_ref;

/// @brief A non-owning reference to a callable, as C++26's std::function_ref (which the device's standard library
/// lacks): it holds the callable's address and a call thunk, never allocates, and must not outlive the callable.
template <class R, class... Args>
class function_ref<R(Args...)> {
public:
	/// @brief Refers to @p f, which must outlive this reference. @p f is a callable object (a lambda, a functor); a
	/// plain function's name is not one: pass a lambda that calls it.
	/// @param f The callable.
	template <class F>
	requires(!std::is_same_v<std::remove_cvref_t<F>, function_ref> && !std::is_function_v<std::remove_reference_t<F>>
	         && std::is_invocable_r_v<R, F&, Args...>)
	function_ref(F&& f) noexcept // NOLINT(google-explicit-constructor): converts like std::function_ref
	    : object_{const_cast<void*>(static_cast<const void*>(std::addressof(f)))},
	      call_{[](void* object, Args... args) -> R {
		      return std::invoke(*static_cast<std::remove_reference_t<F>*>(object), std::forward<Args>(args)...);
	      }} {}

	/// @brief Calls the referred callable.
	R operator()(Args... args) const { return call_(object_, std::forward<Args>(args)...); }

private:
	void* object_;
	R (*call_)(void*, Args...);
};

} // namespace deluge
