#pragma once

#include "Types.hpp"

// Forward-declare the WASM imports we need (also declared in Core.hpp, ImGui.hpp, Config.hpp)
extern "C"
{
    __attribute__((import_module("core"), import_name("is_key_down")))
    int32_t xn_is_key_down(int32_t vkey);

    __attribute__((import_module("core"), import_name("menu_hotkey")))
    int32_t xn_menu_hotkey(const char* label, uint32_t* value);

    __attribute__((import_module("core"), import_name("config_get_int")))
    int32_t xn_config_get_int(const char* key, int32_t defaultValue);

    __attribute__((import_module("core"), import_name("config_set_int")))
    void xn_config_set_int(const char* key, int32_t value);
}

namespace xenon
{
    // Hotkey — wraps a virtual key code with edge detection and menu/config helpers.
    //
    // Usage:
    //   static Hotkey aimKey(VK::RButton);
    //
    //   // on_load:
    //   aimKey.Load("aim_key");
    //
    //   // on_frame:
    //   aimKey.Update();
    //   if (aimKey.IsDown()) { /* held */ }
    //   if (aimKey.Pressed()) { /* just pressed this frame */ }
    //
    //   // on_menu:
    //   aimKey.Render("Aim Key");
    //
    struct Hotkey
    {
        uint32_t vk = 0;

        Hotkey() = default;
        Hotkey(uint32_t defaultKey) : vk(defaultKey) {}

        // Call once per frame (start of on_frame) to update edge-detection state.
        void Update()
        {
            bool down = vk != 0 && xn_is_key_down(static_cast<int32_t>(vk)) != 0;
            m_pressed  = down && !m_down;
            m_released = !down && m_down;
            m_down     = down;
        }

        // True while the key is held down (after Update).
        bool IsDown() const { return m_down; }

        // True for one frame when the key is first pressed (after Update).
        bool Pressed() const { return m_pressed; }

        // True for one frame when the key is released (after Update).
        bool Released() const { return m_released; }

        // Toggle helper — flips `state` on press, returns true if toggled.
        bool Toggle(bool& state)
        {
            if (m_pressed)
            {
                state = !state;
                return true;
            }
            return false;
        }

        // Render keybind widget in on_menu. Returns true if the binding changed.
        bool Render(const char* label)
        {
            bool changed = xn_menu_hotkey(label, &vk) != 0;
            if (changed)
                m_down = m_pressed = m_released = false;
            return changed;
        }

        // Load bound key from plugin config. Uses current vk as default.
        void Load(const char* key)
        {
            vk = static_cast<uint32_t>(xn_config_get_int(key, static_cast<int32_t>(vk)));
        }

        // Save bound key to plugin config.
        void Save(const char* key) const
        {
            xn_config_set_int(key, static_cast<int32_t>(vk));
        }

    private:
        bool m_down     = false;
        bool m_pressed  = false;
        bool m_released = false;
    };
}
