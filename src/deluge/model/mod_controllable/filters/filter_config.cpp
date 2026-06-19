#include "filter_config.h"
#include "gui/l10n/strings.h" // l10n::String (getMorphName, out-of-line so the header stays gui-free)
#include "mem_functions.h"
#include "util/container/enum_to_string_map.hpp"

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

EnumStringMap<FilterMode, kNumFilterModes> filterMap({{{FilterMode::TRANSISTOR_12DB, "12dB"},
                                                       {FilterMode::TRANSISTOR_24DB, "24dB"},
                                                       {FilterMode::TRANSISTOR_24DB_DRIVE, "24dBDrive"},
                                                       {FilterMode::SVF_BAND, "SVF_Band"},
                                                       {FilterMode::SVF_NOTCH, "SVF_Notch"},
                                                       {FilterMode::HPLADDER, "HPLadder"}}});

EnumStringMap<FilterRoute, kNumFilterRoutes> routeMap({{
    {FilterRoute::LOW_TO_HIGH, "L2H"},
    {FilterRoute::PARALLEL, "PARA"},
    {FilterRoute::HIGH_TO_LOW, "H2L"},
}});

} // namespace deluge::dsp::filter

char const* filterRouteToString(FilterRoute route) {
	return deluge::dsp::filter::routeMap(route);
}

FilterRoute stringToFilterRoute(char const* string) {
	return deluge::dsp::filter::routeMap(string);
}

FilterMode stringToLPFType(char const* string) {
	return deluge::dsp::filter::filterMap(string);
}
char const* lpfTypeToString(FilterMode mode) {
	return deluge::dsp::filter::filterMap(mode);
}
