#ifndef NETUDP_PLATFORM_H
#define NETUDP_PLATFORM_H

/**
 * @file platform.h
 * @brief Platform and compiler detection macros.
 */

/* --- Platform --- */

#if defined(_WIN32) || defined(_WIN64)
    #define NETUDP_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define NETUDP_PLATFORM_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define NETUDP_PLATFORM_MACOS 1
#elif defined(__ANDROID__)
    #define NETUDP_PLATFORM_ANDROID 1
#elif defined(__ios__)
    #define NETUDP_PLATFORM_IOS 1
#else
    #error "Unsupported platform"
#endif

/* --- Compiler --- */

#if defined(_MSC_VER)
    #define NETUDP_COMPILER_MSVC 1
#elif defined(__clang__)
    #define NETUDP_COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define NETUDP_COMPILER_GCC 1
#else
    #define NETUDP_COMPILER_UNKNOWN 1
#endif

/* --- Architecture --- */

#if defined(__x86_64__) || defined(_M_X64)
    #define NETUDP_ARCH_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define NETUDP_ARCH_ARM64 1
#elif defined(__i386__) || defined(_M_IX86)
    #define NETUDP_ARCH_X86 1
#endif

/* --- DLL export/import --- */

#if defined(NETUDP_PLATFORM_WINDOWS)
    #if defined(NETUDP_BUILDING_DLL)
        #define NETUDP_EXPORT __declspec(dllexport)
    #elif defined(NETUDP_USING_DLL)
        #define NETUDP_EXPORT __declspec(dllimport)
    #else
        #define NETUDP_EXPORT
    #endif
#else
    #define NETUDP_EXPORT __attribute__((visibility("default")))
#endif

/* --- Hints --- */

#if defined(NETUDP_COMPILER_GCC) || defined(NETUDP_COMPILER_CLANG)
    #define NETUDP_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define NETUDP_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define NETUDP_INLINE      __attribute__((always_inline)) inline
#else
    #define NETUDP_LIKELY(x)   (x)
    #define NETUDP_UNLIKELY(x) (x)
    #define NETUDP_INLINE      __forceinline
#endif

#endif /* NETUDP_PLATFORM_H */
