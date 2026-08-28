#include "shell/profile.h"

namespace deluge::ui::shell {

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
