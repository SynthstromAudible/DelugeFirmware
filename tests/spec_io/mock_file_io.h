// tests/spec_io/mock_file_io.h
#pragma once

extern "C" {
#include "libdeluge/file_io.h"
}

/// Test-only: clears all mock state. Call at the start of every spec that
/// touches the mock, so cases don't see each other's files/directories.
void mock_file_io_reset();

/// @brief Test-only: force @p path to fail with @p status on open/dir_open/mkdir/rename, even when
///        the path EXISTS in the mock.
///
/// Models a `DELUGE_ERR_BUSY` rejection (the Rust BSP's, while it had a worker fiber), where the
/// filesystem refuses to answer rather than reporting absence.
/// @param path   The path to fail; for rename, matches `old_path`.
/// @param status The status to return in place of the normal result.
void mock_file_io_inject_status(const char* path, DelugeStatus status);

/// @brief Test-only: drop all injections set via mock_file_io_inject_status().
///
/// Also done by mock_file_io_reset().
void mock_file_io_clear_injections();
