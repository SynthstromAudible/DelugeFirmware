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
/// - audio_clip::Attack
/// - arpeggiator::midi_cv::Gate
/// - arpeggiator::midi_cv::RatchetAmount
/// - arpeggiator::midi_cv::RatchetProbability
/// - arpeggiator::midi_cv::Rate
/// - arpeggiator::midi_cv::Rhythm
/// - osc::PulseWidth
/// - patched_param::Integer
/// - patched_param::Pan
/// - reverb::Pan
/// - unpatched_param::Pan
/// - unpatched_param::UnpatchedParam
///
/// Done, but current value == final value so no function
/// - arpeggiator::ArpMpeVelocity
/// - arpeggiator::Mode
/// - arpeggiator::NoteMode
/// - arpeggiator::NoteModeFromOctave
/// - arpeggiator::OctaveMode
/// - arpeggiator::OctaveModeToNote
/// - arpeggiator::Octaves
/// - arpeggiator::PresetMode
/// - audio_clip::Reverse
///
/// Special cases:
/// - arpeggiator::Sync - uses syncTypeAndLevelToMenuOption() to pack two values,
///   and menuOptionToSyncType|Level() to unpack them.
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

/** Scales UINT32 range to 0-50 for display.
 *
 * Note UNSIGNED input!
 *
 * Is well behaved for whole UINT32 range despite the final value
 * computation not utilizing the whole range.
 *
 * While both ratchets and rhythm use this, there doesn't seeem
 * to be a clear abstraction or category they embody which leads
 * to this computation, particularly with the final value
 * computation not utilizing the whole range -- otherwise
 * we could call these ...ForUnsignedMenuItem, maybe?
 */
int32_t computeCurrentValueForArpMidiCvRatchetsOrRhythm(uint32_t value);

/** Scales 0-50 range to 0-(UINT32-45) for storage and use.
 *
 * Note UNSIGNED output!
 *
 * See comment in the current value computation above for more.
 */
uint32_t computeFinalValueForArpMidiCvRatchetsOrRhythm(int32_t value);
