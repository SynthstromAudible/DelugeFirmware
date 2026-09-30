// tests/unit/io/mock_stream_io.h -- see mock_stream_io.cpp.
#pragma once

#include <cstdint>

/// Forget every open the mock has counted.
void mock_stream_io_reset();

/// How many times deluge_efatfs_stream_open has been called since the last reset.
uint32_t mock_stream_io_opens();
