#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// these are implemented in c_lib_alternatives instead of cstring, and we need this header to avoid some import order
// shenanigans
void* memset(void*, int, size_t);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int strcmp(const char* str1, const char* str2);
#ifdef __cplusplus
}
#endif
