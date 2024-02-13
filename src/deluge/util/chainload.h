#include <cstdint>

#define OFF_USER_CODE_START 0x20
#define OFF_USER_CODE_END 0x24
#define OFF_USER_CODE_EXECUTE 0x28
#define OFF_USER_SIGNATURE 0x2c

/// Chainload from a buffer.
///
/// The buffer is expected to be in the same format as a Deluge firmware .bin. Namely:
///   - The code start address is located at buffer + OFF_USER_CODE_START
///   - The code end address (inclusive) is at buffer + OFF_USER_CODE_END
///   - The entrypoint address is at a buffer + OFF_USER_CODE_EXECUTE
///
/// Currently only implemented for ARM.
void chainload_from_buf(uint8_t* buffer, int buf_size);
