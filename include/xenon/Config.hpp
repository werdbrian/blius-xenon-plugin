#pragma once
#include "Types.hpp"
#include "String.hpp"

// ============================================================================
// WASM Imports - Config Functions
// ============================================================================

extern "C"
{
    __attribute__((import_module("core"), import_name("config_set_int")))
    void xn_config_set_int(const char* key, int32_t value);

    __attribute__((import_module("core"), import_name("config_set_float")))
    void xn_config_set_float(const char* key, float value);

    __attribute__((import_module("core"), import_name("config_set_bool")))
    void xn_config_set_bool(const char* key, int32_t value);

    __attribute__((import_module("core"), import_name("config_get_int")))
    int32_t xn_config_get_int(const char* key, int32_t defaultValue);

    __attribute__((import_module("core"), import_name("config_get_float")))
    float xn_config_get_float(const char* key, float defaultValue);

    __attribute__((import_module("core"), import_name("config_get_bool")))
    int32_t xn_config_get_bool(const char* key, int32_t defaultValue);

    __attribute__((import_module("core"), import_name("config_save")))
    void xn_config_save();
}

// ============================================================================
// C++ Config Namespace
// ============================================================================

namespace xenon
{
    namespace Config
    {
        inline void SetInt(const char* key, int32_t value) { xn_config_set_int(key, value); }
        inline void SetFloat(const char* key, float value) { xn_config_set_float(key, value); }
        inline void SetBool(const char* key, bool value) { xn_config_set_bool(key, value ? 1 : 0); }

        inline int32_t GetInt(const char* key, int32_t defaultValue = 0) { return xn_config_get_int(key, defaultValue); }
        inline float GetFloat(const char* key, float defaultValue = 0.f) { return xn_config_get_float(key, defaultValue); }
        inline bool GetBool(const char* key, bool defaultValue = false) { return xn_config_get_bool(key, defaultValue ? 1 : 0) != 0; }

        inline void Save() { xn_config_save(); }

        // Load a Color from 4 int keys (key_r, key_g, key_b, key_a)
        inline Color GetColor(const char* key, Color defaultColor = Color::White())
        {
            char buf[128];

            strcpy(buf, key); strcat(buf, "_r");
            uint8_t r = static_cast<uint8_t>(GetInt(buf, defaultColor.R()));

            strcpy(buf, key); strcat(buf, "_g");
            uint8_t g = static_cast<uint8_t>(GetInt(buf, defaultColor.G()));

            strcpy(buf, key); strcat(buf, "_b");
            uint8_t b = static_cast<uint8_t>(GetInt(buf, defaultColor.B()));

            strcpy(buf, key); strcat(buf, "_a");
            uint8_t a = static_cast<uint8_t>(GetInt(buf, defaultColor.A()));

            return Color(r, g, b, a);
        }

        // Save a Color as 4 int keys (key_r, key_g, key_b, key_a)
        inline void SetColor(const char* key, Color color)
        {
            char buf[128];

            strcpy(buf, key); strcat(buf, "_r");
            SetInt(buf, color.R());

            strcpy(buf, key); strcat(buf, "_g");
            SetInt(buf, color.G());

            strcpy(buf, key); strcat(buf, "_b");
            SetInt(buf, color.B());

            strcpy(buf, key); strcat(buf, "_a");
            SetInt(buf, color.A());
        }
    }
}
