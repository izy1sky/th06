#pragma once

#include <cstring>
#include "inttypes.hpp"
#include <SDL2/SDL_endian.h>
#include <type_traits>

static_assert(
    SDL_BYTEORDER == SDL_LIL_ENDIAN ||
    SDL_BYTEORDER == SDL_BIG_ENDIAN
    , "System endian must be either big or little!"
);

static_assert(
    SDL_FLOATWORDORDER == SDL_LIL_ENDIAN ||
    SDL_FLOATWORDORDER == SDL_BIG_ENDIAN
    , "Float endian must be either big or little!"
);

template <typename T>
using UIForSize = std::conditional_t<sizeof(T) == sizeof(u8), u8,
                  std::conditional_t<sizeof(T) == sizeof(u16), u16,
                  std::conditional_t<sizeof(T) == sizeof(u32), u32,
                  std::conditional_t<sizeof(T) == sizeof(u64), u64,
                  void>>>>;

#if defined(__GNUC__) || defined(__clang__)
#define UNALIGNED_ATTR __attribute__((packed))
#elif defined _MSC_VER
#define UNALIGNED_ATTR __declspec(align(1))
#else
#define UNALIGNED_ATTR
#define NEEDS_FALLBACK
#endif

template <typename T> struct UNALIGNED_ATTR Unaligned
{
    T data;

    inline constexpr operator T() const
    {
// In nearly every case, the compiler will inline this memcpy and generate the same code
//   as it would with a packed / unaligned attribute, except GCC ARM32 with mno-unaligned-access,
//   which seems to be weirdly bad at inlining memcpy in general. That makes all of the compiler-specific
//   macro-ing necessary to get codegen that doesn't suck :(
#ifdef NEEDS_FALLBACK
        T ret;
        std::memcpy(&ret, (int *)&data, sizeof(T));
        return ret;
#else
        return data;
#endif
    }
};

#undef UNALIGNED_ATTR
#undef NEEDS_FALLBACK

template <typename T>
static inline constexpr UIForSize<T> bit_cast_from_size(void *value) {
    return *(Unaligned<UIForSize<T>> *)value;
}

template <typename T>
static inline constexpr T bit_cast_to_size(UIForSize<T> value) {
    T ret;
    std::memcpy(&ret, &value, sizeof(value));
    return ret;
}

static constexpr inline u8  ZunByteswap(u8 in)  { return in; }
static constexpr inline u16 ZunByteswap(u16 in) { return SDL_Swap16(in); }
static constexpr inline u32 ZunByteswap(u32 in) { return SDL_Swap32(in); }
static constexpr inline u64 ZunByteswap(u64 in) { return SDL_Swap64(in); }

template <typename T>
struct LE {
    T raw;

    inline constexpr operator T() const {
        UIForSize<T> ui = bit_cast_from_size<T>((void *)&raw);

        if constexpr (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            ui = ZunByteswap(ui);
        }

        return bit_cast_to_size<T>(ui);
    }

    inline constexpr LE &operator=(const T &a) {
        UIForSize<T> ui = bit_cast_from_size<T>((void *)&a);

        if constexpr (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            ui = ZunByteswap(ui);
        }

        raw = bit_cast_to_size<T>(ui);
        return *this;
    }
};

template <>
struct LE<float> {
    float raw;

    inline constexpr operator float() const {
        UIForSize<float> ui = bit_cast_from_size<float>((void*) &raw);

        if constexpr (SDL_FLOATWORDORDER == SDL_BIG_ENDIAN) {
            ui = ZunByteswap(ui);
        }

        return bit_cast_to_size<float>(ui);
    }

    inline constexpr LE &operator=(const float &a) {
        UIForSize<float> ui = bit_cast_from_size<float>((void *)&a);

        if constexpr (SDL_FLOATWORDORDER == SDL_BIG_ENDIAN) {
            ui = ZunByteswap(ui);
        }

        raw = bit_cast_to_size<float>(ui);
        return *this;
    }
};
