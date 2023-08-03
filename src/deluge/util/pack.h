#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pack_8bit_to_7bit(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len);
int32_t unpack_7bit_to_8bit(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len);

int32_t pack_8to7_rle(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len);
int32_t unpack_7to8_rle(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len);

void init_crc_table(void);
uint32_t get_crc(uint8_t* buf, int len);

#ifdef __cplusplus
}
#endif
