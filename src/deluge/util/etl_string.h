/*
 * Helpers for building text into an `etl::string` / `etl::istring` — the inline,
 * non-allocating string type used as the display/builder buffer in place of `StringBuf`.
 *
 * These preserve the exact output of the legacy `StringBuf::append*` formatters (which wrap
 * `intToString` / `intToHex` / `floatToString`), so call sites converted from `StringBuf`
 * stay byte-identical. `etl::string`'s own `append` already covers `const char*`, `char`,
 * and `etl::string_view`; this header adds the codebase's `std::string_view` vocabulary type
 * plus the integer/float formatters.
 */
#pragma once
#include "etl/string.h"
#include <cstdint>
#include <string_view>

namespace deluge::string {

/// Append `sv` to `out`. Bridges `std::string_view` (the codebase vocabulary type) onto etl's
/// `string_view`-based append, which does not accept `std::string_view` directly.
inline void append(etl::istring& out, std::string_view sv) {
	out.append(sv.data(), sv.size());
}

/// Append a base-10 integer, left-padded with '0' to at least `minDigits`.
/// Matches `StringBuf::appendInt` / `intToString`.
void appendInt(etl::istring& out, int32_t number, int32_t minDigits = 1);

/// Append a hexadecimal integer in a field of at least `minDigits`.
/// Matches `StringBuf::appendHex` / `intToHex`.
void appendHex(etl::istring& out, uint32_t number, int32_t minDigits = 1);

/// Append a float with between `minDecimals` and `maxDecimals` fractional digits (trailing
/// zeros trimmed down to `minDecimals`). Matches `StringBuf::appendFloat` / `floatToString`.
void appendFloat(etl::istring& out, float number, int32_t minDecimals, int32_t maxDecimals);

/// Remove all whitespace characters from `s`, in place. Matches `StringBuf::removeSpaces`.
void removeSpaces(etl::istring& s);

} // namespace deluge::string
