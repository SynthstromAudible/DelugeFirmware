#pragma once
#include "loop_point.h"

namespace menu_item::sample {
class End final : public LoopPoint {
public:
	End(char const* newName = NULL) : LoopPoint(newName) { markerType = MARKER_END; }
};
}
