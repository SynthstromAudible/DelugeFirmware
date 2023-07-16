#include <new>
#include <stdexcept>

inline void* operator new(std::size_t) {
    extern void *DO_NOT_USE_UNARY_NEW();
    return DO_NOT_USE_UNARY_NEW();
}
inline void* operator new(std::size_t, std::nothrow_t) noexcept {
    extern void *DO_NOT_USE_UNARY_NEW();
    return DO_NOT_USE_UNARY_NEW();
}
inline void* operator new[](std::size_t) {
    extern void *DO_NOT_USE_UNARY_NEW();
    return DO_NOT_USE_UNARY_NEW();
}
inline void* operator new[](std::size_t, std::nothrow_t) noexcept{
    extern void *DO_NOT_USE_UNARY_NEW();
    return DO_NOT_USE_UNARY_NEW();
};
