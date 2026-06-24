// Minimal load test — draws static text, no entity calls.
// If you see "PLUGIN OK" on screen, loading works.

#include <xenon/SDK.hpp>
using namespace xenon;

XENON_PLUGIN_INFO(
    "ingame_test",
    "Ingame Test",
    "Xenon",
    "Minimal load test.",
    "1.0",
    0,
    PluginFlags::HasOverlay
)

extern "C" void on_load()              { Log("ingame_test: on_load"); }
extern "C" void on_unload()            {}
extern "C" void on_frame(float dt)     { (void)dt; }

extern "C" void on_render()
{
    Draw::Text(10.f, 10.f, Color::Green(), "PLUGIN OK", 16);
}
