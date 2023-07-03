#pragma once
#include "loop_point.h"

namespace menu_item::sample {
class Start final : public LoopPoint {
public:
	Start(char const* newName = NULL) : LoopPoint(newName) { markerType = MARKER_START; }
};
} // namespace menu_item::sample
