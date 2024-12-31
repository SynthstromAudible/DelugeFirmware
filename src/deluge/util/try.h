#pragma once
#include <utility>

#define D_TRY(expr)                                                                                                    \
	({                                                                                                                 \
		auto result = (expr);                                                                                          \
		if (!result.has_value()) [[unlikely]] {                                                                        \
			return std::unexpected(result.error());                                                                    \
		}                                                                                                              \
		result.value();                                                                                                \
	})

#define D_TRY_CATCH(expr, error_var, block)                                                                            \
	({                                                                                                                 \
		auto result = (expr);                                                                                          \
		if (!result.has_value()) [[unlikely]] {                                                                        \
			auto error_var = result.error();                                                                           \
			block                                                                                                      \
		}                                                                                                              \
		result.value();                                                                                                \
	})

#define D_TRY_MOVE(expr)                                                                                               \
	({                                                                                                                 \
		auto result = (expr);                                                                                          \
		if (!result.has_value()) [[unlikely]] {                                                                        \
			return std::unexpected(result.error());                                                                    \
		}                                                                                                              \
		std::move(result.value());                                                                                     \
	})

#define D_TRY_CATCH_MOVE(expr, error_var, block)                                                                       \
	({                                                                                                                 \
		auto result = (expr);                                                                                          \
		if (!result.has_value()) [[unlikely]] {                                                                        \
			auto error_var = result.error();                                                                           \
			block                                                                                                      \
		}                                                                                                              \
		std::move(result.value());                                                                                     \
	})
