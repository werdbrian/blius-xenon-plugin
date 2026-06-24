// math_helpers_lib.cpp — Example library plugin
// Build with: build.bat --library examples\math_helpers_lib.cpp
//
// This plugin provides reusable math utility functions that other plugins
// can call via the dependency system. It exports functions under the
// "math_helpers" service name.

#include <xenon/SDK.hpp>

// Declare this plugin as a library providing "math_helpers"
XENON_LIBRARY_INFO(
    "math_helpers_lib",   // internal name
    "Xenon",              // author
    "Shared math utils",  // description
    "1.0",                // version
    "math_helpers"        // service name
)

// ============================================================================
// Exported library functions
// ============================================================================
// These are the functions that dependent plugins can call through the bridge.
// They must be extern "C" so they get clean export names in the WASM binary.

extern "C" float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

extern "C" float clamp_f(float value, float min_val, float max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

extern "C" float remap(float value, float from_min, float from_max, float to_min, float to_max)
{
    float t = (value - from_min) / (from_max - from_min);
    return to_min + (to_max - to_min) * t;
}

extern "C" float ease_in_quad(float t)
{
    return t * t;
}

extern "C" float ease_out_quad(float t)
{
    return t * (2.0f - t);
}

extern "C" float distance_2d(float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrtf(dx * dx + dy * dy);
}
