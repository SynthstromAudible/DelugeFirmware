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
/// - audio_compressor::CompParam
/// - arpeggiator::midi_cv::Gate
/// - osc::PulseWidth
/// - patched_param::Integer
/// - patched_param::Pan
/// - reverb::Pan
/// - unpatched_param::Pan
/// - unpatched_param::UnpatchedParam
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

/** Scales INT32_MIN-INT32_MAX range to 0-50 for display.
 */
int32_t computeCurrentValueForStandardMenuItem(int32_t value);

/** Scales 0-50 range to INT32_MIN-INT32_MAX for storage and use.
 */
int32_t computeFinalValueForStandardMenuItem(int32_t value);

/** Scales 0-INT32_MAX range to 0-50 for display.
 */
int32_t computeCurrentValueForHalfPrecisionMenuItem(int32_t value);

/** Scales 0-50 range to 0-INT32_MAX for storage and use.
 */
int32_t computeFinalValueForHalfPrecisionMenuItem(int32_t value);

/** Scales INT32_MIN-INT32_MAX range to -25-25 for display.
 */
int32_t computeCurrentValueForPan(int32_t value);

/** Scales -25 to 25 range to INT32_MIN-INT32_MAX for storage and use.
 */
int32_t computeFinalValueForPan(int32_t value);

/** Scales INT32_MIN-INT32_MAX range to 0-50 for display.
 *
 * This roundtrips with the final value math despite not
 * being it's proper inverse.
 */
int32_t computeCurrentValueForArpMidiCvGate(int32_t value);

/** Scales 0-50 range to INT32_MIN-(INT32_MAX-45) for storage and use.
 *
 * This is presumably to have the gate go down even at 50: the values
 * produced create a 2.5ms gate down period between 16th arp notes at Gate=50,
 * which exactly matches the gate down period between regular 16h notes.
 */
int32_t computeFinalValueForArpMidiCvGate(int32_t value);
