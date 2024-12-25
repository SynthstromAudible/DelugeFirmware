#pragma once

#include "definitions_cxx.hpp"

#include <array>
#include <cstdint>

typedef struct {
	uint8_t length; // the number of steps to use, between 1 and 4
	bool steps[6];  // the steps, whether they should play a note or a silence
} ArpRhythm;

// We define an alias for kMaxMenuValue, because the usage is more
// obvious that way.
//
// The connection cannot be trivially broken right, now, but it would
// be nice to do so.
//
// Currently:
// - changing the max menu value means this table is now to wrong size,
//   which the compiler will happily notice (good)
// - adding arp patterns so that table grows larger than kMaxMenuValue
//   will require changing value scaling done by computeCurrentValueForUnsignedMenuItem
// - changing value scaling will re-scaling values from old song file
//
// There's a test that will break if you don't read this comment and
// break the relationship anyhow.
const int32_t kMaxPresetArpRhythm = kMaxMenuValue;
// const uint32_t arpRhythmActiveNotesInPattern[kMaxPresetArpRhythm + 1] = {
// 	1, // <- 1 step
// 	1, // <- 3 steps
// 	2, // <-
// 	2, // <-
// 	3, // <- 4 steps
// 	2, // <-
// 	3, // <-
// 	2, // <-
// 	3, // <-
// 	1, // <- 5 steps
// 	4, // <-
// 	2, // <-
// 	4, // <-
// 	2, // <-
// 	4, // <-
// 	2, // <-
// 	4, // <-
// 	2, // <-
// 	3, // <-
// 	3, // <-
// 	3, // <-
// 	3, // <-
// 	3, // <-
// 	3, // <-
// 	1, // <- 6 steps
// 	5, // <-
// 	2, // <-
// 	5, // <-
// 	2, // <-
// 	5, // <-
// 	2, // <-
// 	5, // <-
// 	2, // <-
// 	5, // <-
// 	4, // <-
// 	3, // <-
// 	4, // <-
// 	3, // <-
// 	4, // <-
// 	3, // <-
// 	4, // <-
// 	3, // <-
// 	4, // <-
// 	3, // <-
// 	4, // <-
// 	3, // <-
// 	4, // <-
// 	3, // <-
// 	3, // <-
// 	3, // <-
// 	4, // <-
// };
const ArpRhythm arpRhythmPatterns[kMaxPresetArpRhythm + 1] = {
    {1, {true, true, true, true, true, true}}, // <- 1 step
    {3, {true, false, false, true, true, true}}, // <- 3 steps
    {3, {true, true, false, true, true, true}}, // <-
    {3, {true, false, true, true, true, true}}, // <-
    {4, {true, false, true, true, true, true}}, // <- 4 steps
    {4, {true, true, false, false, true, true}}, // <-
    {4, {true, true, true, false, true, true}}, // <-
    {4, {true, false, false, true, true, true}}, // <-
    {4, {true, true, false, true, true, true}}, // <-
    {5, {true, false, false, false, false, true}}, // <- 5 steps
    {5, {true, false, true, true, true, true}}, // <-
    {5, {true, true, false, false, false, true}}, // <-
    {5, {true, true, true, true, false, true}}, // <-
    {5, {true, false, false, false, true, true}}, // <-
    {5, {true, true, false, true, true, true}}, // <-
    {5, {true, false, true, false, false, true}}, // <-
    {5, {true, true, true, false, true, true}}, // <-
    {5, {true, false, false, true, false, true}}, // <-
    {5, {true, false, false, true, true, true}}, // <-
    {5, {true, true, true, false, false, true}}, // <-
    {5, {true, true, false, false, true, true}}, // <-
    {5, {true, false, true, true, false, true}}, // <-
    {5, {true, true, false, true, false, true}}, // <-
    {5, {true, false, true, false, true, true}}, // <-
    {6, {true, false, false, false, false, false}}, // <- 6 steps
    {6, {true, false, true, true, true, true}}, // <-
    {6, {true, true, false, false, false, false}}, // <-
    {6, {true, true, true, true, true, false}}, // <-
    {6, {true, false, false, false, false, true}}, // <-
    {6, {true, true, false, true, true, true}}, // <-
    {6, {true, false, true, false, false, false}}, // <-
    {6, {true, true, true, true, false, true}}, // <-
    {6, {true, false, false, false, true, false}}, // <-
    {6, {true, true, true, false, true, true}}, // <-
    {6, {true, false, false, true, true, true}}, // <-
    {6, {true, true, true, false, false, false}}, // <-
    {6, {true, true, true, true, false, false}}, // <-
    {6, {true, false, false, false, true, true}}, // <-
    {6, {true, true, false, false, true, true}}, // <-
    {6, {true, false, true, true, false, false}}, // <-
    {6, {true, true, true, false, false, true}}, // <-
    {6, {true, false, false, true, true, false}}, // <-
    {6, {true, false, true, false, true, true}}, // <-
    {6, {true, true, false, true, false, false}}, // <-
    {6, {true, true, true, false, true, false}}, // <-
    {6, {true, false, false, true, false, true}}, // <-
    {6, {true, false, true, true, true, false}}, // <-
    {6, {true, true, false, false, false, true}}, // <-
    {6, {true, true, false, false, true, false}}, // <-
    {6, {true, false, true, false, false, true}}, // <-
    {6, {true, true, false, true, false, true}}, // <-
};

const std::array<char const*, kMaxPresetArpRhythm + 1> arpRhythmPatternNames = {
    "None", // <- 0, No rhythm: play all notes

    // 3 steps
    "0--", // <- 1

    "00-", // <- 2
    "0-0", // <- 3

    // 4 steps
    "0-00", // <- 4
    "00--", // <- 5

    "000-", // <- 6
    "0--0", // <- 7

    "00-0", // <- 8

    // 5 steps
    "0----", // <- 9

    "0-000", // <- 10
    "00---", // <- 11

    "0000-", // <- 12
    "0---0", // <- 13

    "00-00", // <- 14
    "0-0--", // <- 15

    "000-0", // <- 16
    "0--0-", // <- 17

    "0--00", // <- 18
    "000--", // <- 19

    "00--0", // <- 20
    "0-00-", // <- 21

    "00-0-", // <- 22
    "0-0-0", // <- 23

    // 6 steps
    "0-----", // <- 24

    "0-0000", // <- 25
    "00----", // <- 26

    "00000-", // <- 27
    "0----0", // <- 28

    "00-000", // <- 29
    "0-0---", // <- 30

    "0000-0", // <- 31
    "0---0-", // <- 32

    "000-00", // <- 33

    "0--000", // <- 34
    "000---", // <- 35

    "0000--", // <- 36
    "0---00", // <- 37

    "00--00", // <- 38
    "0-00--", // <- 39

    "000--0", // <- 40
    "0--00-", // <- 41

    "0-0-00", // <- 42
    "00-0--", // <- 43

    "000-0-", // <- 44
    "0--0-0", // <- 45

    "0-000-", // <- 46
    "00---0", // <- 47

    "00--0-", // <- 48
    "0-0--0", // <- 49
    "00-0-0", // <- 50

};
