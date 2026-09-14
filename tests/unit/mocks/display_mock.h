#pragma once

// The unit binary's `display`: a real deluge::hid::Display that shows nothing and remembers the last error it was
// asked to show. The firmware UI code compiled into this binary (gui/ui/slicer.cpp) calls through it, and an empty
// stand-in has no vtable to call through.

#include "definitions_cxx.hpp"
#include "hid/display/display.h"

namespace deluge::test {
/// The error most recently passed to `display->displayError()`, or Error::NONE. Specs reset it themselves.
extern Error lastDisplayedError;
} // namespace deluge::test
