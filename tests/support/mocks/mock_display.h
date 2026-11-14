#pragma once

// The spec binaries' `display`: a real deluge::hid::Display that shows nothing and remembers the last error it was
// asked to show. Firmware UI code compiled into a spec binary calls through it, and an empty stand-in has no vtable to
// call through.

#include "definitions_cxx.hpp"
#include "hid/display/display.h"

namespace deluge::test {
/// The error most recently passed to `display->displayError()`, or Error::NONE. Specs reset it themselves.
extern Error lastDisplayedError;
} // namespace deluge::test
