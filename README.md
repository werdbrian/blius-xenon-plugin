# Xenon Plugin SDK

Build WASM plugins for Xenon using C++.

## Requirements

- LLVM/Clang with WASM target support
- Windows (for the build script)

## IDE Setup

Open the `plugin-sdk/` folder in your editor for full autocomplete, go-to-definition, and type checking.

**VS Code (clangd)** — works out of the box. The `compile_flags.txt` tells clangd the target and include paths. Install the [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) if you don't have it.

**VS Code (Microsoft C/C++)** — add to `.vscode/c_cpp_properties.json`:
```json
{
    "configurations": [{
        "name": "WASM",
        "includePath": ["${workspaceFolder}/include"],
        "compilerArgs": ["--target=wasm32"],
        "cStandard": "c17",
        "cppStandard": "c++17",
        "intelliSenseMode": "clang-x64"
    }]
}
```

**CLion** — open `plugin-sdk/` as a folder project. CLion's built-in clangd will pick up `compile_flags.txt` automatically.

## Quick Start

```batch
cd plugin-sdk
build.bat examples\enemy_esp.cpp     # Build one plugin
build.bat                            # Build all examples
build.bat --library examples\math_helpers_lib.cpp  # Build a library plugin
```

Built plugins are placed in `output/`.

## Minimal Plugin

```cpp
#include <xenon/SDK.hpp>
using namespace xenon;

XENON_PLUGIN("my_esp", "My ESP", "Draws circles on enemies")

extern "C" void on_render()
{
    for (Entity player : Players())
    {
        if (!player.IsAlive() || !player.IsEnemy()) continue;

        Vector2 screen;
        if (WorldToScreen(player.GetHeadPos(), screen))
            Draw::CircleFilled(screen, 5.f, Color::Red());
    }
}
```

## Entry Points

All optional — implement only what you need:

```cpp
extern "C" void on_load()                            // Called when plugin loads
extern "C" void on_unload()                          // Called when plugin unloads
extern "C" void on_frame(float dt)                   // Called every frame (game logic)
extern "C" void on_render()                          // Called every frame (drawing)
extern "C" void on_menu()                            // Called when menu is open (UI)
extern "C" void on_hero_changed(uint64_t heroPoolId) // Called when hero changes
```

## API Overview

### Entity

```cpp
Entity local = LocalPlayer();
for (Entity player : Players()) {
    if (!player.IsAlive() || !player.IsEnemy()) continue;
    if (player.IsVisible()) {
        Vector3 head = player.GetHeadPos();
        float dist = player.GetDistance();
    }
}

Entity target = FindBestTarget(0);
```

### Drawing

```cpp
Draw::Line(100, 100, 200, 200, Color::Red());
Draw::Circle(500, 500, 50, Color::Green());
Draw::CircleFilled(600, 600, 30, Color::Blue());
Draw::Rect(100, 300, 200, 100, Color::White());
Draw::RectFilled(100, 500, 200, 100, Color::Yellow());
Draw::Text(100, 700, Color::White(), "Hello World");
Draw::HealthBar(100, 100, 100, 10, healthPercent);
```

### ImGui

```cpp
if (ImGui::CollapsingHeader("My Settings")) {
    static bool enabled = true;
    static float fov = 60.f;
    static int32_t mode = 0;

    ImGui::Checkbox("Enabled", &enabled);
    ImGui::SliderFloat("FOV", &fov, 10.f, 180.f);
    ImGui::Combo("Mode", &mode, "Mode A\0Mode B\0");
}
```

### Config

```cpp
bool enabled = Config::GetBool("enabled", true);
float fov = Config::GetFloat("fov", 60.f);

Config::SetBool("enabled", enabled);
Config::SetFloat("fov", fov);
Config::Save();
```

### Input & Screen

```cpp
if (IsKeyDown(VK::LButton)) { /* ... */ }

Vector2 screen = ScreenSize();
Vector2 center = ScreenCenter();
```

### World to Screen

```cpp
Vector3 worldPos = enemy.GetHeadPos();
Vector2 screenPos;
if (WorldToScreen(worldPos, screenPos)) {
    Draw::Circle(screenPos, 5.f, Color::Red());
}
```

### Raycast

```cpp
if (IsRaycastReady()) {
    Vector3 eye = LocalPlayer().GetHeadPos();
    Vector3 target = enemy.GetBonePos(Bone::Head);

    // Quick LOS check
    if (IsPointVisible(eye, target))
        Log("Target is visible");

    // Full trace with hit info
    RaycastResult hit = Raycast(eye, target);
    if (hit.IsHit())
        Draw::CircleFilled(WorldToScreen(hit.hitPos), 4.f, Color::Red());
}
```

### Connection

```cpp
int ping = GetClientPing();

// Game-level button injection
PressGameButton(GameButton::Jump);
ReleaseGameButton(GameButton::Jump);
```

## Hero-Specific Plugins

Use `xenon::HeroId` constants from `include/xenon/Types.hpp`:

```cpp
XENON_PLUGIN_INFO("widow_aim", "Widow Aim", "Me", "Aim for Widow", "1.0",
    xenon::HeroId::Widowmaker,
    xenon::PluginFlags::HasOverlay | xenon::PluginFlags::HeroSpecific)
```

## Dependencies

Library plugins let you share code between plugins.

### 1. Build the Library

```batch
build.bat --library examples\math_helpers_lib.cpp
```

Uses `--export-all` so all `extern "C"` functions become WASM exports.

### 2. Build the Dependent Plugin

```batch
build.bat examples\enemy_esp_with_deps.cpp
```

The `#include <xenon/libs/math_helpers.hpp>` import header generates `lib_math_helpers.*` imports via `__attribute__((import_module(...)))`.

### 3. Runtime Flow

- Host downloads both WASMs and introspects imports
- `math_helpers_lib.wasm` has only `core` imports — library candidate
- `enemy_esp_with_deps.wasm` has `lib_math_helpers` imports — needs the library
- Topological sort puts the library first
- Library loads, bridge registers `lerp`, `clamp_f`, `remap` as native functions under `lib_math_helpers`
- Dependent loads, WAMR resolves `lib_math_helpers.*` imports against the bridge

### Key Files

| File | Role |
|------|------|
| `examples/math_helpers_lib.cpp` | Library plugin — uses `XENON_LIBRARY_INFO`, exports `extern "C"` functions |
| `examples/enemy_esp_with_deps.cpp` | Dependent plugin — uses `XENON_PLUGIN_INFO_DEPS` and `#include <xenon/libs/math_helpers.hpp>` |
| `include/xenon/libs/math_helpers.hpp` | Import header — `import_module`/`import_name` attributes + C++ wrappers in `xenon::math` |

## Full Documentation

For detailed API reference, guides, and tutorials, see the [documentation site](../../../xenon-docs/).
