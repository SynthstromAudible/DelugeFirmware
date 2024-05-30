#pragma once

#include <cstdint>

// Functions that convert parameter values between forms displayed
// on the UI (typically 0-50, but there are variations), and
// forms store internally and on SD (typically whole int32_t range.)
//
// computeCurrentValueForXXX() takes the internally stored value
// and computes the value displayed on the UI.
//
// computeFinalValueForXXX() takes the value displayed on the UI
// and computes the internally stored value.
//
// These are in non-member functions for ease of testing. The param
// classes pulls in a lot of stuff!
//
// Generally speaking the call chains are:
//
//   SomeClass::readCurrentValue() -> computeCurrentValueForSomeClass()
//
//   SomeClass::writeCurrentValue() -> SomeClass::getFinalValue() ->
//     -> computeFinalValueForSomeClass()
//
// Right now only UnpatchedParam, but more is coming. As stuff is
// is extraced and turns out to be functionally identical the dupes
// will be eliminated.
//
// Then when we have only functionally distinct variations here (and
// we have tests for them), then we can replace them with a parametrized
// version.
//
// ...and then it should be easier to make changes like specifying envelope
// times in milliseconds and bitcrushing in bits, hopefully without
// needing specialized code to handle existing saves.

int32_t computeCurrentValueForUnpatchedParam(int32_t value);
int32_t computeFinalValueForUnpatchedParam(int32_t value);
