#include <xenon/SDK.hpp>
using namespace xenon;

XENON_PLUGIN_INFO(
    "LocalPlayerTest", "LocalPlayerTest", "Xenon",
    "Entity API crash reproduction in training area.",
    "1.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

extern "C" void on_load()  { Log("LocalPlayerTest loaded"); }
extern "C" void on_unload() {}

static float g_timer = 0.f;

extern "C" void on_frame(float dt)
{
    g_timer += dt;
    if (g_timer < 3.f) return;
    g_timer = 0.f;

    Log("--- entity test ---");
    Log("IsIngame: start");
    bool inGame = IsIngame();
    TextBuilder<32> tb;
    tb.put("IsIngame: ").putInt(inGame);
    Log(tb.c_str());

    Log("Players iterator: start");
    int count = 0;
    for (Entity p : Players())
    {
        count++;
        tb.clear(); tb.put("  player ").putInt(count).put(" isEnemy=").putInt(p.IsEnemy());
        Log(tb.c_str());
    }
    tb.clear(); tb.put("Players iterator: done, count=").putInt(count);
    Log(tb.c_str());

    Log("FindBestTarget: start");
    Entity t = FindBestTarget(TargetFlags::Enemy);
    Log("FindBestTarget: returned");
    tb.clear(); tb.put("FindBestTarget valid: ").putInt(t.IsValid());
    Log(tb.c_str());

    Log("--- end ---");
}

extern "C" void on_menu()
{
    ImGui::Text("entity crash repro");
}
