#include "model/scale/utils.h"
#include "definitions_cxx.hpp"
#include "util/const_functions.h"

bool isSameNote(int16_t a, int16_t b) {
	return (a - b) % kOctaveSize == 0;
}
