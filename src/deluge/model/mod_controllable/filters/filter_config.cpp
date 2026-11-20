#include "filter_config.h"
#include "mem_functions.h"

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
