#include "filter_config.h"
#include "gui/l10n/strings.h" // l10n::String (getMorphName, out-of-line so the header stays gui-free)
#include "mem_functions.h"

// converts lpf/hpf mode to string for saving
namespace deluge::dsp::filter {

l10n::String SpecificFilter::getMorphName(bool shortName) const {
	switch (family) {
	case FilterFamily::LP_LADDER:
		return l10n::String::STRING_FOR_DRIVE;
	case FilterFamily::HP_LADDER:
		return l10n::String::STRING_FOR_FM;
	case FilterFamily::SVF:
		return shortName ? l10n::String::STRING_FOR_MORPH_SHORT : l10n::String::STRING_FOR_MORPH;
	default:
		return l10n::String::STRING_FOR_NONE;
	}
}

} // namespace deluge::dsp::filter

char const* filterRouteToString(FilterRoute route) {
	return deluge::effects::to_string(route).data(); // a view of a string literal
}

FilterRoute stringToFilterRoute(char const* string) {
	return deluge::effects::parse_filter_route(string);
}

FilterMode stringToLPFType(char const* string) {
	return deluge::effects::parse_filter_mode(string);
}
char const* lpfTypeToString(FilterMode mode) {
	return deluge::effects::to_string(mode).data(); // a view of a string literal
}
