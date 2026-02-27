#include "model/sequencing/matriceal_engine.h"

int16_t MatricealLane::currentValue() const {
	if (length == 0) {
		return 0;
	}
	return values[position];
}

void MatricealLane::advance() {
	if (length <= 1) {
		return;
	}

	switch (direction) {
	case LaneDirection::FORWARD:
		position = (position + 1) % length;
		break;

	case LaneDirection::REVERSE:
		if (position == 0) {
			position = length - 1;
		}
		else {
			position--;
		}
		break;

	case LaneDirection::PINGPONG:
		if (!pingpongReversed) {
			if (position >= length - 1) {
				pingpongReversed = true;
				position--;
			}
			else {
				position++;
			}
		}
		else {
			if (position == 0) {
				pingpongReversed = false;
				position++;
			}
			else {
				position--;
			}
		}
		break;
	}
}

void MatricealLane::reset() {
	position = 0;
	pingpongReversed = false;
}
