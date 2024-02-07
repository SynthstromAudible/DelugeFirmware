#include "filter_config.h"
#include "definitions_cxx.hpp"
#include "mem_functions.h"
#include "util/container/enum_to_string_map.hpp"

// converts lpf/hpf mode to string for saving
namespace deluge::dsp::filter {

EnumStringMap<FilterMode, kNumFilterModes> filterMap({{{FilterMode::TRANSISTOR_12DB, "12dB"},
                                                       {FilterMode::TRANSISTOR_24DB, "24dB"},
                                                       {FilterMode::TRANSISTOR_24DB_DRIVE, "24dBDrive"},
                                                       {FilterMode::SVF_BAND, "SVF_Band"},
                                                       {FilterMode::SVF_NOTCH, "SVF_Notch"},
                                                       {FilterMode::HPLADDER, "HPLadder"}}});

} // namespace deluge::dsp::filter

FilterMode stringToLPFType(char const* string) {
	return deluge::dsp::filter::filterMap(string);
}
char const* lpfTypeToString(FilterMode mode) {
	return deluge::dsp::filter::filterMap(mode);
}
