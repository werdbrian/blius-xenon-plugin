// live_values.cpp
// Dumps raw API values to HUD every frame so you can see exactly what the SDK is returning.

#include <xenon/SDK.hpp>

using namespace xenon;

XENON_PLUGIN_INFO(
    "live_values",
    "Live Values",
    "Xenon",
    "Dumps raw SDK values to HUD for debugging.",
    "1.0",
    0,
    PluginFlags::HasOverlay
)

extern "C" void on_load()   { Log("live_values loaded"); }
extern "C" void on_unload() {}

extern "C" void on_frame(float dt)
{
    (void)dt;
    static float timer = 0.f;
    timer += dt;
    if (timer < 2.f) return;
    timer = 0.f;

    TextBuilder<128> tb;

    // Core
    tb.clear(); tb.put("[LV] IsIngame: ").put(IsIngame() ? "TRUE" : "FALSE");
    Log(tb.c_str());

    tb.clear(); tb.put("[LV] GetPlayerCount: ").putInt(GetPlayerCount());
    Log(tb.c_str());

    tb.clear(); tb.put("[LV] GetCurrentHero: ").putInt((int)GetCurrentHero());
    Log(tb.c_str());

    tb.clear(); tb.put("[LV] GetMapId: ").putInt((int)GetMapId());
    Log(tb.c_str());

    tb.clear(); tb.put("[LV] GetTime: ").putFloat(GetTime(), 1);
    Log(tb.c_str());

    // Local player
    Entity local = LocalPlayer();
    tb.clear(); tb.put("[LV] LocalPlayer valid: ").put(local.IsValid() ? "YES" : "NO");
    Log(tb.c_str());

    if (local.IsValid())
    {
        tb.clear(); tb.put("[LV] Local HP: ").putFloat(local.GetHealth(), 0).put(" / ").putFloat(local.GetHealthMax(), 0);
        Log(tb.c_str());

        Vector3 pos = local.GetPosition();
        tb.clear(); tb.put("[LV] Local Pos: ").putFloat(pos.x,1).put(" ").putFloat(pos.y,1).put(" ").putFloat(pos.z,1);
        Log(tb.c_str());
    }

    // All players
    int i = 0;
    for (Entity p : Players())
    {
        tb.clear();
        tb.put("[LV] P[").putInt(i).put("] ")
          .put("alive=").putInt(p.IsAlive())
          .put(" local=").putInt(p.IsLocal())
          .put(" enemy=").putInt(p.IsEnemy())
          .put(" ally=").putInt(p.IsAlly())
          .put(" hp=").putFloat(p.GetHealth(), 0)
          .put(" idx=").putInt(p.Index());
        Log(tb.c_str());
        i++;
        if (i >= 6) break;
    }

    if (i == 0)
        Log("[LV] Players() returned 0 entities");
}

extern "C" void on_render()
{
    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    const float X    = sz.x * 0.5f + 10.f; // right of center
    const float LINE = 14.f;
    const float W    = 260.f;
    float y = 20.f;

    // Background
    Draw::RectFilled(X - 4.f, y - 4.f, W, LINE * 22 + 8.f, Color(0, 0, 0, 170));

    TextBuilder<64> tb;

    // Header
    Draw::Text(X, y, Color::Cyan(), "--- LIVE VALUES ---", 13);
    y += LINE + 2.f;

    // Core
    tb.clear(); tb.put("IsIngame:        ").put(IsIngame() ? "TRUE" : "FALSE");
    Draw::Text(X, y, IsIngame() ? Color::Green() : Color::Red(), tb.c_str(), 12); y += LINE;

    tb.clear(); tb.put("GetTime:         ").putFloat(GetTime(), 1);
    Draw::Text(X, y, Color::White(), tb.c_str(), 12); y += LINE;

    tb.clear(); tb.put("ScreenSize:      ").putFloat(sz.x, 0).put("x").putFloat(sz.y, 0);
    Draw::Text(X, y, Color::White(), tb.c_str(), 12); y += LINE;

    tb.clear(); tb.put("GetCurrentHero:  ").putInt((int)GetCurrentHero());
    Draw::Text(X, y, GetCurrentHero() ? Color::Green() : Color::Red(), tb.c_str(), 12); y += LINE;

    tb.clear(); tb.put("GetMapId:        ").putInt((int)GetMapId());
    Draw::Text(X, y, Color::White(), tb.c_str(), 12); y += LINE;

    tb.clear(); tb.put("GetClientPing:   ").putInt(GetClientPing());
    Draw::Text(X, y, Color::White(), tb.c_str(), 12); y += LINE;

    tb.clear(); tb.put("GetSensitivity:  ").putFloat(GetSensitivity(), 2);
    Draw::Text(X, y, Color::White(), tb.c_str(), 12); y += LINE;

    y += 4.f;
    Draw::Text(X, y, Color::Cyan(), "--- PLAYERS ---", 13); y += LINE;

    // Entity
    int total = GetPlayerCount();
    tb.clear(); tb.put("GetPlayerCount:  ").putInt(total);
    Draw::Text(X, y, total > 0 ? Color::Green() : Color::Red(), tb.c_str(), 12); y += LINE;

    // Iterate and show first 3 players
    int shown = 0;
    for (Entity p : Players())
    {
        if (shown >= 3) break;

        TextBuilder<64> row;
        row.put("[").putInt(p.Index()).put("] ")
           .put(p.IsLocal() ? "LOCAL " : "      ")
           .put(p.IsEnemy() ? "ENM " : "    ")
           .put(p.IsAlly()  ? "ALY " : "    ")
           .put(p.IsAlive() ? "LIVE " : "DEAD ")
           .put("HP:").putFloat(p.GetHealth(), 0);

        Color c = p.IsLocal() ? Color::Cyan()
                : p.IsEnemy() ? Color::Red()
                : p.IsAlly()  ? Color::Green()
                : Color::White();

        Draw::Text(X, y, c, row.c_str(), 11);
        y += LINE;
        shown++;
    }

    if (shown == 0)
    {
        Draw::Text(X, y, Color::Yellow(), "  (no players iterated)", 11);
        y += LINE;
    }

    y += 4.f;
    Draw::Text(X, y, Color::Cyan(), "--- LOCAL PLAYER ---", 13); y += LINE;

    Entity local = LocalPlayer();
    if (local.IsValid())
    {
        Vector3 pos = local.GetPosition();
        tb.clear(); tb.put("Pos: ").putFloat(pos.x,0).put(" ").putFloat(pos.y,0).put(" ").putFloat(pos.z,0);
        Draw::Text(X, y, Color::White(), tb.c_str(), 11); y += LINE;

        tb.clear(); tb.put("Health: ").putFloat(local.GetHealth(), 0).put(" / ").putFloat(local.GetHealthMax(), 0);
        Draw::Text(X, y, Color::White(), tb.c_str(), 11); y += LINE;

        tb.clear(); tb.put("Hero: ").putInt((int)local.GetHeroId());
        Draw::Text(X, y, Color::White(), tb.c_str(), 11); y += LINE;
    }
    else
    {
        Draw::Text(X, y, Color::Red(), "  LocalPlayer: INVALID", 11);
    }
}
