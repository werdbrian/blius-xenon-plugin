#pragma once
#include "Types.hpp"
#include "String.hpp"

// ============================================================================
// WASM Imports - Menu Functions
// ============================================================================

extern "C"
{
    __attribute__((import_module("core"), import_name("menu_checkbox")))
    int32_t xn_menu_checkbox(const char* label, uint8_t* value);

    __attribute__((import_module("core"), import_name("menu_slider_int")))
    int32_t xn_menu_slider_int(const char* label, int32_t* value, int32_t min, int32_t max);

    __attribute__((import_module("core"), import_name("menu_slider_float")))
    int32_t xn_menu_slider_float(const char* label, float* value, float min, float max);

    __attribute__((import_module("core"), import_name("menu_combo")))
    int32_t xn_menu_combo(const char* label, int32_t* value, const char* items);

    __attribute__((import_module("core"), import_name("menu_text")))
    void xn_menu_text(const char* text);

    __attribute__((import_module("core"), import_name("menu_tooltip")))
    void xn_menu_tooltip(const char* text);

    __attribute__((import_module("core"), import_name("menu_separator")))
    void xn_menu_separator();

    __attribute__((import_module("core"), import_name("menu_collapsing_header")))
    int32_t xn_menu_collapsing_header(const char* label);

    __attribute__((import_module("core"), import_name("menu_hotkey")))
    int32_t xn_menu_hotkey(const char* label, uint32_t* value);

    __attribute__((import_module("core"), import_name("menu_color_picker")))
    int32_t xn_menu_color_picker(const char* label, uint32_t* argbColor);
}

// ============================================================================
// ImGui Wrapper Namespace
// ============================================================================

namespace xenon
{
    namespace ImGui
    {
        // Checkbox - uses bool wrapper
        inline bool Checkbox(const char* label, bool* value)
        {
            uint8_t intValue = *value ? 1 : 0;
            bool changed = xn_menu_checkbox(label, &intValue) != 0;
            *value = (intValue != 0);
            return changed;
        }

        // Sliders
        inline bool SliderFloat(const char* label, float* value, float min, float max)
        {
            return xn_menu_slider_float(label, value, min, max) != 0;
        }

        inline bool SliderInt(const char* label, int32_t* value, int32_t min, int32_t max)
        {
            return xn_menu_slider_int(label, value, min, max) != 0;
        }

        // Combo (null-separated items string)
        inline bool Combo(const char* label, int32_t* currentItem, const char* items)
        {
            return xn_menu_combo(label, currentItem, items) != 0;
        }

        // Text
        inline void Text(const char* text)
        {
            xn_menu_text(text);
        }

        // Tooltip (on last item)
        inline void Tooltip(const char* text)
        {
            xn_menu_tooltip(text);
        }

        // Layout
        inline void Separator()
        {
            xn_menu_separator();
        }

        // Collapsing header
        inline bool CollapsingHeader(const char* label)
        {
            return xn_menu_collapsing_header(label) != 0;
        }

        // Hotkey bind widget — displays current key binding and allows rebinding
        // Returns true when the bound key changes
        inline bool Hotkey(const char* label, uint32_t* vkCode)
        {
            return xn_menu_hotkey(label, vkCode) != 0;
        }

        // Color picker — swatch button that opens a color picker popup. Returns true if changed.
        // Alpha channel is preserved but not editable in the picker.
        inline bool ColorPicker(const char* label, Color* color)
        {
            return xn_menu_color_picker(label, &color->value) != 0;
        }

        // RGBA color picker — 4 sliders (R/G/B/A). Returns true if any changed.
        inline bool ColorSliders(const char* label, Color* color)
        {
            int32_t r = color->R(), g = color->G(), b = color->B(), a = color->A();
            bool changed = false;

            char buf[128];

            strcpy(buf, label); strcat(buf, " R");
            changed |= SliderInt(buf, &r, 0, 255);

            strcpy(buf, label); strcat(buf, " G");
            changed |= SliderInt(buf, &g, 0, 255);

            strcpy(buf, label); strcat(buf, " B");
            changed |= SliderInt(buf, &b, 0, 255);

            strcpy(buf, label); strcat(buf, " A");
            changed |= SliderInt(buf, &a, 0, 255);

            if (changed)
                *color = Color(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                               static_cast<uint8_t>(b), static_cast<uint8_t>(a));
            return changed;
        }
    }
}
