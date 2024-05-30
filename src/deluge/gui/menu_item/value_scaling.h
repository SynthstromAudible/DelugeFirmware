#pragma once

#include <cstdint>

/// \file value_scaling.h.
///
/// Functions that convert parameter values between forms displayed
/// on the UI (typically 0-50, but there are variations), and
/// forms stored internally and on SD (typically whole int32_t range.)
///
/// computeCurrentValueForXXX() takes the internally stored value
/// and computes the value displayed on the UI.
///
/// computeFinalValueForXXX() takes the value displayed on the UI
/// and computes the internally stored value.
///
/// These are in non-member functions for ease of testing. The param
/// classes pulls in a lot of stuff!
///
/// Generally speaking the call chains are:
///
///   SomeClass::readCurrentValue() -> computeCurrentValueForSomeClass()
///
///   SomeClass::writeCurrentValue() -> SomeClass::getFinalValue() ->
///     -> computeFinalValueForSomeClass()
///
/// Done:
/// - UnpatchedParam
/// - patched_param::Integer
///
/// As stuff is extraced and turns out to be functionally identical the dupes
/// should be eliminated.
///
/// When we have all the functionally distinct variations here, and we have
/// tests for them, then we can replace them with a parametrized version or
/// two.
///
/// ...and then it should be easier to make changes like specifying envelope
/// times in seconds and bitcrushing in bits, hopefully without
/// needing specialized code to handle existing saves -- or at least have
/// unit tests for the conversion code if it is needed.

/** Scales int32_t range to 0-50 for display.
 */
int32_t computeCurrentValueForStandardMenuItem(int32_t value);

/** Scales 0-50 range to int32_t for storage and use.
 */
int32_t computeFinalValueForStandardMenuItem(int32_t value);
