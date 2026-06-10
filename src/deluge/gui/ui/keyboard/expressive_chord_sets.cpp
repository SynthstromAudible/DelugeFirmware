/*
 * Copyright © 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "gui/ui/keyboard/expressive_chord_sets.h"

namespace deluge::gui::ui::keyboard {

// 32 chords per set on 8×4 grid; bottom-left pad at (x=2, y=0). Roots are MIDI before transpose.
const std::array<ExpressiveChordSet, kExpressiveChordSetCount> expressiveChordSets = {{
    {"Pop",
     {{
         // bottom row → top row (indices 0–7, 8–15, 16–23, 24–31)
         {&kMajor, 0, 60},  {&kMinor, 0, 57},  {&kMajor, 0, 53}, {&kMajor, 0, 55}, {&kMinor7, 0, 57}, {&k7, 0, 55},
         {&kMajor, 0, 52},  {&kMinor, 0, 64},  {&kSus2, 0, 60},  {&kSus4, 0, 60},  {&k6, 0, 60},      {&kM7, 0, 60},
         {&kMinor7, 0, 62}, {&k9, 0, 60},      {&kMajor, 1, 60}, {&kMinor, 1, 57}, {&k2, 0, 60},      {&k69, 0, 60},
         {&k7Sus2, 0, 55},  {&kM9, 0, 60},     {&k11, 0, 60},    {&k13, 0, 60},    {&kMajor, 0, 48},  {&kMinor, 0, 55},
         {&k7, 0, 52},      {&kMinor7, 0, 53}, {&kSus2, 0, 57},  {&kSus4, 0, 57},  {&k6, 0, 55},      {&kM7, 0, 57},
         {&k9, 0, 55},      {&kMajor, 1, 53},
     }}},
    {"Soul",
     {{
         {&kMinor7, 0, 60}, {&kMinor9, 0, 60},  {&k7, 0, 58},       {&kM7, 0, 58},      {&kMinor7, 0, 55},
         {&k9, 0, 55},      {&kMinor7, 0, 53},  {&kM7, 0, 53},      {&kMinor7, 0, 62},  {&k11, 0, 60},
         {&k13, 0, 60},     {&kMinor11, 0, 60}, {&k7Sus4, 0, 60},   {&kMinor6, 0, 60},  {&kMinor7, 1, 60},
         {&k9, 1, 55},      {&kMinor9, 0, 57},  {&kMinor11, 0, 57}, {&kMinor13, 0, 60}, {&k7Sus2, 0, 58},
         {&kM9, 0, 58},     {&kMinor6, 0, 57},  {&kMinor7, 0, 50},  {&k9, 0, 53},       {&kMinor7, 0, 48},
         {&kM7, 0, 60},     {&kMinor7, 1, 57},  {&k11, 0, 55},      {&k13, 0, 55},      {&kMinor9, 0, 55},
         {&k7, 0, 53},      {&kMinor7, 0, 58},
     }}},
    {"Jazz",
     {{
         {&kM7, 0, 60},       {&kMinor7, 0, 57},   {&kMinor7b5, 0, 60}, {&kMinor7, 0, 53},     {&kM7, 0, 55},
         {&k7, 0, 50},        {&kMinor7, 0, 62},   {&kM9, 0, 60},       {&kMinor9, 0, 57},     {&kM11, 0, 60},
         {&kMinor11, 0, 57},  {&kM13, 0, 60},      {&kMinor13, 0, 57},  {&kDim, 0, 60},        {&kM7, 1, 60},
         {&kMinor7, 1, 57},   {&kMinor7b5, 0, 58}, {&kM7, 0, 58},       {&kMinor7, 0, 55},     {&kM9, 0, 57},
         {&kMinor9, 0, 55},   {&kM11, 0, 57},      {&kMinor11, 0, 55},  {&kM13Sharp11, 0, 60}, {&kMinor7b5, 0, 53},
         {&kFullDim, 0, 60},  {&kAug, 0, 60},      {&k7, 0, 55},        {&kMinor7, 0, 50},     {&kM7, 0, 53},
         {&kMinor7b5, 0, 62}, {&kM9, 0, 55},
     }}},
    {"House",
     {{
         {&kMinor, 0, 60},  {&kMinor, 0, 57},  {&kMinor7, 0, 60}, {&kMinor7, 0, 55}, {&kSus2, 0, 60},
         {&kSus4, 0, 60},   {&k7, 0, 58},      {&kMinor7, 0, 58}, {&kMinor, 0, 53},  {&kMinor7, 0, 53},
         {&kMinor, 0, 62},  {&kMinor7, 0, 62}, {&kMinor, 0, 64},  {&k7Sus2, 0, 60},  {&kMinor7, 1, 60},
         {&kSus2, 1, 60},   {&kMinor, 0, 55},  {&kMinor7, 0, 57}, {&kSus4, 0, 57},   {&k7, 0, 55},
         {&kMinor7, 0, 50}, {&kMinor, 0, 48},  {&kMinor7, 0, 48}, {&kSus2, 0, 55},   {&k7Sus4, 0, 58},
         {&kMinor7, 1, 57}, {&kMinor, 0, 52},  {&kMinor7, 0, 52}, {&kSus2, 0, 58},   {&kSus4, 0, 58},
         {&k7, 0, 53},      {&kMinor7, 0, 64},
     }}},
    {"Techno",
     {{
         {&kMinor, 0, 57},  {&kMinor, 0, 55},  {&kMinor, 0, 53},  {&kMinor, 0, 60}, {&kMinor7, 0, 57},
         {&kMinor7, 0, 55}, {&kMinor7, 0, 53}, {&kMinor7, 0, 60}, {&kSus2, 0, 57},  {&kSus4, 0, 57},
         {&kSus2, 0, 60},   {&kSus4, 0, 60},   {&kDim, 0, 57},    {&kMinor, 0, 62}, {&kMinor7, 0, 62},
         {&k7, 0, 55},      {&kSus2, 0, 55},   {&kSus4, 0, 55},   {&kMinor, 0, 50}, {&kMinor7, 0, 50},
         {&kMinor, 0, 48},  {&kMinor7, 0, 48}, {&kSus2, 0, 53},   {&kSus4, 0, 53},  {&kMinor, 1, 57},
         {&kMinor7, 1, 57}, {&kSus2, 1, 60},   {&k7Sus2, 0, 57},  {&kMinor, 0, 64}, {&kMinor7, 0, 64},
         {&kSus2, 0, 58},   {&kDim, 0, 60},
     }}},
    {"Deep",
     {{
         {&kMinor7, 0, 60}, {&kMinor9, 0, 60}, {&kMinor7, 0, 57},  {&kMinor9, 0, 57}, {&kMinor7, 0, 55},
         {&kMinor9, 0, 55}, {&kSus2, 0, 60},   {&kSus4, 0, 60},    {&kMinor6, 0, 60}, {&k7Sus2, 0, 58},
         {&k7Sus4, 0, 58},  {&kM7, 0, 58},     {&kMinor7, 0, 53},  {&kMinor9, 0, 53}, {&kMinor7, 0, 62},
         {&kMinor9, 0, 62}, {&k11, 0, 60},     {&kMinor11, 0, 60}, {&k9, 0, 60},      {&k13, 0, 60},
         {&kMinor7, 1, 60}, {&kMinor9, 1, 60}, {&kSus2, 1, 60},    {&kMinor6, 0, 57}, {&kMinor7, 0, 50},
         {&kMinor9, 0, 50}, {&k7, 0, 55},      {&kMinor7, 0, 58},  {&kM9, 0, 58},     {&kMinor13, 0, 60},
         {&kMinor7, 0, 48}, {&kMinor9, 0, 48},
     }}},
    {"Bop",
     {{
         {&kMinor7, 0, 62},   {&k7, 0, 65},          {&kM7, 0, 60},     {&kMinor7b5, 0, 59},  {&k7, 0, 62},
         {&kMinor7, 0, 57},   {&kM7, 0, 53},         {&kMinor7, 0, 55}, {&kM9, 0, 60},        {&kMinor9, 0, 57},
         {&kM11, 0, 60},      {&kMinor11, 0, 57},    {&kM13, 0, 60},    {&kMinor13, 0, 57},   {&kDim, 0, 62},
         {&kAug, 0, 60},      {&kMinor7b5b9, 0, 60}, {&k7, 0, 58},      {&kMinorMaj7, 0, 57}, {&kMinor7b5, 0, 53},
         {&kM7, 0, 58},       {&kMinor7, 0, 50},     {&kM9, 0, 55},     {&kMinor9, 0, 55},    {&kM13Sharp11, 0, 60},
         {&kMinor9b5, 0, 60}, {&kFullDim, 0, 59},    {&k7Sus4, 0, 62},  {&kMinor7, 1, 57},    {&kM7, 1, 60},
         {&kMinor7b5, 0, 62}, {&k7, 0, 55},
     }}},
}};

} // namespace deluge::gui::ui::keyboard
