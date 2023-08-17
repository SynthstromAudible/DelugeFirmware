#include "definitions_cxx.hpp"

#define OFF_USER_CODE_START 0x20
#define OFF_USER_CODE_END 0x24
#define OFF_USER_CODE_EXECUTE 0x28
#define OFF_USER_SIGNATURE 0x2c

void chainload_from_buf(uint8_t* buffer, int buf_size);
