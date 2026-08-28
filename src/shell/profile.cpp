#include "shell/profile.h"

namespace deluge::ui::shell {

namespace {

/// @brief Spell \p value in decimal into \p scratch, right-aligned, and view it.
///
/// Hand-rolled rather than `std::to_string` (which allocates) or `snprintf` (which drags in the
/// whole formatting machinery for two call sites): `src/shell` allocates nothing, and the widest
/// input is five digits and a sign.
/// @param value   The number to spell.
/// @param scratch The caller's buffer; the returned view points into it.
/// @return The digits, with a leading `-` for a negative \p value.
std::string_view render_digits(int32_t value, TextScratch& scratch) {
	char* const end = scratch.chars + sizeof(scratch.chars);
	char* p = end;
	// Through `uint32_t`, so the most negative input has no magnitude to overflow into.
	uint32_t magnitude = value < 0 ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value);
	do {
		*--p = static_cast<char>('0' + magnitude % 10u);
		magnitude /= 10u;
	} while (magnitude != 0u);
	if (value < 0) {
		*--p = '-';
	}
	return std::string_view{p, static_cast<size_t>(end - p)};
}

} // namespace

std::string_view BoardProfile::text_or_digits(uint16_t string_id, TextScratch& scratch) const {
	return text != nullptr ? text(string_id) : render_digits(static_cast<int32_t>(string_id), scratch);
}

std::string_view BoardProfile::error_text_or_digits(int16_t error_code, TextScratch& scratch) const {
	return error_text != nullptr ? error_text(error_code) : render_digits(error_code, scratch);
}

static constexpr const char* kButtonNames[] = {
    "None",  "AffectEntire", "Session",   "Clip",     "Synth", "Kit",   "Midi",     "Cv",          "Keyboard",
    "Scale", "CrossScreen",  "Back",      "Load",     "Save",  "Learn", "TapTempo", "SyncScaling", "Triplets",
    "Play",  "Record",       "Shift",     "Mod0",     "Mod1",  "Mod2",  "Mod3",     "Mod4",        "Mod5",
    "Mod6",  "Mod7",         "SelectEnc", "TempoEnc", "XEnc",  "YEnc",  "ModEnc0",  "ModEnc1"};
static_assert(sizeof(kButtonNames) / sizeof(kButtonNames[0]) == static_cast<size_t>(ButtonId::Count));

const char* button_name(ButtonId b) {
	return static_cast<size_t>(b) < static_cast<size_t>(ButtonId::Count) ? kButtonNames[static_cast<size_t>(b)] : "?";
}

bool ButtonSet::has(ButtonId b) const {
	return (bits >> static_cast<unsigned>(b)) & 1u;
}

void ButtonSet::set(ButtonId b, bool on) {
	const uint64_t m = uint64_t{1} << static_cast<unsigned>(b);
	bits = on ? (bits | m) : (bits & ~m);
}

bool ButtonSet::any() const {
	return bits != 0;
}

} // namespace deluge::ui::shell
