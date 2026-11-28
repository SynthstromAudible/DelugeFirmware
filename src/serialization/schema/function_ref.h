#pragma once

/// @file
/// @brief A non-owning reference to a callable, for a callback that does not outlive the call it is passed to.

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace deluge::serialization::schema {

template <typename Signature>
class FunctionRef;

/// @brief A non-owning reference to a callable taking @p Args and returning @p R. The callable must outlive the
/// reference; pass a lambda straight to the function that takes it.
/// @tparam R    The return type.
/// @tparam Args The parameter types.
template <typename R, typename... Args>
class FunctionRef<R(Args...)> {
public:
	/// @brief A reference to @p callable.
	/// @param callable The callable; its call operator must be const (a lambda's is unless it is mutable).
	template <typename F>
	requires(!std::is_same_v<F, FunctionRef> && std::is_invocable_r_v<R, const F&, Args...>)
	// NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor): a lambda passes as one
	FunctionRef(const F& callable) noexcept
	    : object_{std::addressof(callable)}, call_{[](const void* object, Args... args) -> R {
		      return std::invoke(*static_cast<const F*>(object), std::forward<Args>(args)...);
	      }} {}

	/// @brief Call the referenced callable.
	/// @param args The arguments.
	/// @return What it returns.
	R operator()(Args... args) const { return call_(object_, std::forward<Args>(args)...); }

private:
	const void* object_;
	R (*call_)(const void*, Args...);
};

} // namespace deluge::serialization::schema
