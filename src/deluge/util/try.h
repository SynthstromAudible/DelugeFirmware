#pragma once

#define D_TRY(expr)                                                                                                    \
	({                                                                                                                 \
		auto result = (expr);                                                                                          \
		if (!result.has_value()) {                                                                                     \
			return std::unexpected(result.error());                                                                    \
		}                                                                                                              \
		result.value();                                                                                                \
	})
