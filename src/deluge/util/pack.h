#include "definitions.h"

int pack_8bit_to_7bit(uint8_t* dst, int dst_size, uint8_t* src, int src_len);
int unpack_7bit_to_8bit(uint8_t* dst, int dst_size, uint8_t* src, int src_len);

int pack_8to7_rle(uint8_t* dst, int dst_size, uint8_t* src, int src_len);
int unpack_7to8_rle(uint8_t* dst, int dst_size, uint8_t* src, int src_len);
