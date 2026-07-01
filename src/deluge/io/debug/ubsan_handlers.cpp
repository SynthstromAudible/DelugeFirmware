/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

// Freestanding UndefinedBehaviorSanitizer runtime. Enabled by the DELUGE_UBSAN CMake option, which adds
// -fsanitize=undefined to the deluge target. GCC then emits calls to these __ubsan_handle_* hooks at each potential UB
// site (out-of-bounds indexing, signed overflow, bad shifts, null/misaligned derefs, ...). There is no libubsan on bare
// metal, so we supply our own hooks; each one prints the source location UBSAN handed us and freezes. This catches the
// undefined behaviour that frequently *causes* the heap corruption the MEM_GUARD allocator checks hunt for.

#include "definitions.h"

#if DELUGE_UBSAN

#include "io/debug/log.h"
#include <cstdint>

namespace {
// Every __ubsan_handle_* "data" argument begins with a SourceLocation, so we can recover file:line:col by treating the
// first argument as one of these. (A couple of handlers carry the relevant location in a later field; for those the
// printed location may be the declaration site rather than the use site, which is still useful.)
struct SourceLocation {
	const char* filename;
	uint32_t line;
	uint32_t column;
};

[[noreturn]] void ubsanReport(const SourceLocation* loc, const char* kind) {
	if (loc != nullptr && loc->filename != nullptr) {
		D_PRINTLN("UBSAN: %s at %s:%d:%d", kind, loc->filename, loc->line, loc->column);
	}
	else {
		D_PRINTLN("UBSAN: %s (no source location)", kind);
	}
	FREEZE_WITH_ERROR("UBSN");
	while (true) {} // FREEZE_WITH_ERROR does not return, but keep the [[noreturn]] contract explicit
}
} // namespace

// The data pointer (whose first field is the SourceLocation) is always the first argument; the trailing value arguments
// are ignored. GCC carries built-in declarations of these hooks with fixed arities, so each definition must match the
// expected number of parameters exactly - hence the per-arity macros.
extern "C" {

#define UBSAN_H1(name, kind)                                                                                           \
	void name(void* data) {                                                                                            \
		ubsanReport(static_cast<const SourceLocation*>(data), kind);                                                   \
	}
#define UBSAN_H2(name, kind)                                                                                           \
	void name(void* data, void*) {                                                                                     \
		ubsanReport(static_cast<const SourceLocation*>(data), kind);                                                   \
	}
#define UBSAN_H3(name, kind)                                                                                           \
	void name(void* data, void*, void*) {                                                                              \
		ubsanReport(static_cast<const SourceLocation*>(data), kind);                                                   \
	}

UBSAN_H2(__ubsan_handle_type_mismatch_v1, "type mismatch / misaligned or null pointer")
UBSAN_H3(__ubsan_handle_add_overflow, "signed addition overflow")
UBSAN_H3(__ubsan_handle_sub_overflow, "signed subtraction overflow")
UBSAN_H3(__ubsan_handle_mul_overflow, "signed multiplication overflow")
UBSAN_H2(__ubsan_handle_negate_overflow, "negation overflow")
UBSAN_H3(__ubsan_handle_divrem_overflow, "division / remainder overflow")
UBSAN_H3(__ubsan_handle_shift_out_of_bounds, "shift out of bounds")
UBSAN_H2(__ubsan_handle_out_of_bounds, "array index out of bounds")
UBSAN_H2(__ubsan_handle_load_invalid_value, "load of invalid value (e.g. bad bool/enum)")
UBSAN_H1(__ubsan_handle_invalid_builtin, "invalid builtin use")
UBSAN_H3(__ubsan_handle_pointer_overflow, "pointer arithmetic overflow")
UBSAN_H1(__ubsan_handle_nonnull_arg, "null passed to nonnull argument")
UBSAN_H2(__ubsan_handle_nonnull_return_v1, "null returned from nonnull function")
UBSAN_H2(__ubsan_handle_vla_bound_not_positive, "non-positive VLA bound")
UBSAN_H2(__ubsan_handle_float_cast_overflow, "float cast overflow")
// builtin_unreachable / missing_return carry the compiler's own const+noreturn attributes; ubsanReport never returns,
// so leaving them unannotated here is fine (control never actually falls off the end).
UBSAN_H1(__ubsan_handle_builtin_unreachable, "reached __builtin_unreachable")
UBSAN_H1(__ubsan_handle_missing_return, "missing return from value-returning function")

#undef UBSAN_H1
#undef UBSAN_H2
#undef UBSAN_H3

} // extern "C"

#endif
