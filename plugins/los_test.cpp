#include <xenon/SDK.hpp>
using namespace xenon;

XENON_PLUGIN_INFO(
    "lostest", "LOS Test", "c",
    "Diagnostic: shows whether IsVisible / IsPointVisible / Raycast actually detect walls",
    "1.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ─────────────────────────────────────────────
//  Settings
// ─────────────────────────────────────────────

static bool  g_enabled = true;
static bool  g_useHead = true;   // aim point: head bone vs body bone

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────

extern "C" void on_load()
{
    g_enabled = Config::GetBool("enabled", true);
    g_useHead = Config::GetBool("useHead", true);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled", g_enabled);
    Config::SetBool("useHead", g_useHead);
    Config::Save();
}

extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled", &g_enabled);
    if (!g_enabled) return;
    ImGui::Checkbox("Use head bone (else body)", &g_useHead);
    ImGui::Separator();
    ImGui::Text("Stand so a WALL is between you and an enemy.");
    ImGui::Text("Watch the HUD: if the checks flip to BLOCKED,");
    ImGui::Text("LOS works. If they stay VISIBLE/1.00, still broken.");
}

// ─────────────────────────────────────────────
//  on_render — pick nearest enemy, run all 3 LOS checks
// ─────────────────────────────────────────────

extern "C" void on_render()
{
    if (!g_enabled) return;

    float x = 12.f, y = 12.f;
    Draw::TextShadow(x, y, Color::White(), "[ LOS Test ]"); y += 20.f;

    if (!IsIngame())
    {
        Draw::TextShadow(x, y, Color(150,150,150,200), "not in game");
        return;
    }

    Vector3 camPos;
    bool haveCam = GetCameraPosition(camPos);

    // Find nearest alive enemy
    int   count    = GetPlayerCount();
    if (count > 32) count = 32;
    int   bestIdx  = -1;
    float bestDist = 1e9f;
    Entity local   = Entity::Local();
    Vector3 lpos   = local.GetPosition();

    for (int i = 0; i < count; ++i)
    {
        Entity e(i);
        if (!e.IsValid() || e.IsLocal() || !e.IsAlive()) continue;
        if (!e.IsEnemy()) continue;
        float d = e.GetDistance();
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }

    if (bestIdx < 0)
    {
        Draw::TextShadow(x, y, Color(150,150,150,200), "no enemy found");
        return;
    }

    Entity tgt(bestIdx);
    int     bone   = g_useHead ? Bone::Head : Bone::Body;
    Vector3 tgtPos = tgt.GetBonePos(bone);

    TextBuilder<128> tb;

    // Distance / bone
    tb.put("Target: dist ").putFloat(bestDist, 1).put("m  bone ").put(g_useHead ? "HEAD" : "BODY");
    Draw::TextShadow(x, y, Color::White(), tb.c_str()); y += 18.f; tb.clear();

    y += 4.f;

    // ── 1) Entity IsVisible() ──────────────────────────────────────────
    bool entVis = tgt.IsVisible();
    tb.put("1) Entity.IsVisible(): ").put(entVis ? "VISIBLE" : "BLOCKED");
    Draw::TextShadow(x, y, entVis ? Color::Green() : Color::Red(), tb.c_str());
    y += 18.f; tb.clear();

    // ── 2) IsPointVisible(cam -> target) ───────────────────────────────
    if (haveCam)
    {
        bool ptVis = IsPointVisible(camPos, tgtPos);
        tb.put("2) IsPointVisible():   ").put(ptVis ? "VISIBLE" : "BLOCKED");
        Draw::TextShadow(x, y, ptVis ? Color::Green() : Color::Red(), tb.c_str());
        y += 18.f; tb.clear();
    }
    else
    {
        Draw::TextShadow(x, y, Color(150,150,150,200), "2) IsPointVisible():   no cam pos");
        y += 18.f;
    }

    // ── 3) Raycast(cam -> target).fraction ─────────────────────────────
    if (haveCam)
    {
        RaycastResult rc = Raycast(camPos, tgtPos);
        tb.put("3) Raycast frac ").putFloat(rc.fraction, 2)
          .put(rc.IsHit() ? "  HIT" : "  clear");
        Draw::TextShadow(x, y, rc.IsHit() ? Color::Red() : Color::Green(), tb.c_str());
        y += 18.f; tb.clear();
    }

    // Raycast subsystem readiness
    y += 4.f;
    bool rcReady = IsRaycastReady();
    tb.put("Raycast ready: ").put(rcReady ? "YES" : "NO");
    Draw::TextShadow(x, y, rcReady ? Color::Green() : Color(200,160,0,255), tb.c_str());
    y += 18.f; tb.clear();

    y += 6.f;
    Draw::TextShadow(x, y, Color(180,180,180,220),
        "Put a wall between you & enemy.");
    y += 16.f;
    Draw::TextShadow(x, y, Color(180,180,180,220),
        "Flips RED = LOS works.  Stays GREEN = broken.");
}
