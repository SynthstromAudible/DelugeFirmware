#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Declared here rather than through <cstring> to avoid some import order shenanigans; the C library provides them.
void* memset(void*, int, size_t);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int strcmp(const char* str1, const char* str2);
#ifdef __cplusplus
}
#endif
