#pragma once

// Xenon Plugin SDK
// Include this single header to get all SDK functionality

#include "Types.hpp"
#include "Memory.hpp"
#include "String.hpp"
#include "Format.hpp"
#include "Core.hpp"
#include "Entity.hpp"
#include "Draw.hpp"
#include "ImGui.hpp"
#include "Config.hpp"
#include "Hotkey.hpp"

// ============================================================================
// Plugin Entry Points
// ============================================================================

// Plugins should implement these functions (all optional):
//
// extern "C" void on_load()
//     Called when plugin is loaded. Initialize state here.
//
// extern "C" void on_unload()
//     Called when plugin is unloaded. Clean up here.
//
// extern "C" void on_frame(float deltaTime)
//     Called every frame. Do game logic here.
//
// extern "C" void on_render()
//     Called every frame during render phase. Do drawing here.
//
// extern "C" void on_menu()
//     Called when menu is open. Create ImGui UI here.
//
// extern "C" void on_hero_changed(uint64_t heroPoolId)
//     Called when local player's hero changes.
//
// extern "C" void on_get_info(PluginInfo* info)
//     Called to get plugin metadata. Fill in the struct.

// ============================================================================
// Plugin Info Structure
// ============================================================================

namespace xenon
{
    // Plugin metadata structure (for on_get_info)
    struct PluginInfo
    {
        const char* name;         // Internal name (no spaces)
        const char* author;       // Author name
        const char* description;  // Short description
        const char* version;      // Version string
        uint64_t targetHeroId;    // 0 = universal, or specific hero pool ID
        uint32_t flags;           // PluginFlags (see Types.hpp)
        const char* dependencies; // Null-separated, double-null terminated list of required service names (or nullptr)
        const char* provides;     // Service name this plugin provides (or nullptr if not a library)
    };
}

// ============================================================================
// Convenience Macros for Plugin Definition
// ============================================================================

// Define plugin info with a simple macro
#define XENON_PLUGIN_INFO(NAME, DISPLAY, AUTHOR, DESC, VERSION, HERO_ID, FLAGS) \
    extern "C" __attribute__((export_name("on_get_info"))) void on_get_info(xenon::PluginInfo* info) { \
    info->name = NAME; \
    info->author = AUTHOR; \
    info->description = DESC; \
    info->version = VERSION; \
    info->targetHeroId = HERO_ID; \
    info->flags = FLAGS; \
    info->dependencies = nullptr; \
    info->provides = nullptr; \
    }

// Simpler macro for universal plugins
#define XENON_PLUGIN(NAME, DISPLAY, DESC) \
    XENON_PLUGIN_INFO(NAME, DISPLAY, "Unknown", DESC, "1.0", 0, \
        xenon::PluginFlags::HasOverlay | xenon::PluginFlags::HasMenu)

// Define a library plugin that provides a service
// PROVIDES: service name string (e.g., "math_helpers")
#define XENON_LIBRARY_INFO(NAME, AUTHOR, DESC, VERSION, PROVIDES) \
    extern "C" void on_get_info(xenon::PluginInfo* info) { \
        info->name = NAME; \
        info->author = AUTHOR; \
        info->description = DESC; \
        info->version = VERSION; \
        info->targetHeroId = 0; \
        info->flags = 0; \
        info->dependencies = nullptr; \
        info->provides = PROVIDES; \
    }

// Define plugin info with dependencies
// DEPS: null-separated, double-null terminated string of dependency service names
//   e.g., "math_helpers\0ui_widgets\0" (the trailing \0 is the double-null terminator)
#define XENON_PLUGIN_INFO_DEPS(NAME, DISPLAY, AUTHOR, DESC, VERSION, HERO_ID, FLAGS, DEPS) \
    extern "C" void on_get_info(xenon::PluginInfo* info) { \
        info->name = NAME; \
        info->author = AUTHOR; \
        info->description = DESC; \
        info->version = VERSION; \
        info->targetHeroId = HERO_ID; \
        info->flags = FLAGS; \
        info->dependencies = DEPS; \
        info->provides = nullptr; \
    }
