#pragma once

#define D_TRY(expr)                                                                                                    \
	({                                                                                                                 \
		auto result = (expr);                                                                                          \
		if (!result.has_value()) {                                                                                     \
			return std::unexpected(result.error());                                                                    \
		}                                                                                                              \
		result.value();                                                                                                \
	})

#define D_TRY_CATCH(expr, error_var, block)                                                                            \
	({                                                                                                                 \
		auto result = (expr);                                                                                          \
		if (!result.has_value()) {                                                                                     \
			auto error_var = result.error();                                                                           \
			block                                                                                                      \
		}                                                                                                              \
		result.value();                                                                                                \
	})
