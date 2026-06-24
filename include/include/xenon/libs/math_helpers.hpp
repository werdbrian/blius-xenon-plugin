#pragma once
// Example library import header for the "math_helpers" service.
//
// Library authors distribute this header alongside their compiled WASM library.
// Plugin developers include it and call the inline wrapper functions.
//
// The library plugin must:
//   1. Be compiled with `build.bat --library`
//   2. Use XENON_LIBRARY_INFO(..., "math_helpers") in its on_get_info
//   3. Export the raw functions (lerp, clamp_f, remap)
//
// The host automatically bridges these exports as native functions under
// the "lib_math_helpers" import module when the library is loaded.

// Raw WASM imports from the "lib_math_helpers" module
extern "C"
{
    __attribute__((import_module("lib_math_helpers"), import_name("lerp")))
    float lib_math_helpers_lerp(float a, float b, float t);

    __attribute__((import_module("lib_math_helpers"), import_name("clamp_f")))
    float lib_math_helpers_clamp_f(float value, float min_val, float max_val);

    __attribute__((import_module("lib_math_helpers"), import_name("remap")))
    float lib_math_helpers_remap(float value, float from_min, float from_max, float to_min, float to_max);
}

// Roots every import above so wasm-ld --gc-sections can't drop them when -O2
// folds every wrapper call site to a constant. Never invoked at runtime.
namespace xenon::detail
{
    __attribute__((used))
    inline void _math_helpers_keepalive()
    {
        volatile float r;
        r = lib_math_helpers_lerp(0.f, 0.f, 0.f);
        r = lib_math_helpers_clamp_f(0.f, 0.f, 0.f);
        r = lib_math_helpers_remap(0.f, 0.f, 0.f, 0.f, 0.f);
        (void)r;
    }
}

// Convenient C++ wrappers
namespace xenon::math
{
    inline float Lerp(float a, float b, float t) { return lib_math_helpers_lerp(a, b, t); }
    inline float Clamp(float value, float min_val, float max_val) { return lib_math_helpers_clamp_f(value, min_val, max_val); }
    inline float Remap(float value, float from_min, float from_max, float to_min, float to_max)
    {
        return lib_math_helpers_remap(value, from_min, from_max, to_min, to_max);
    }
}
