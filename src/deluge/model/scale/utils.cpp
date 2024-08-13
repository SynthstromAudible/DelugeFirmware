#include "model/scale/utils.h"
#include "util/const_functions.h"

bool isSameNote(int16_t a, int16_t b) {
	return (a - b) % 12 == 0;
}
