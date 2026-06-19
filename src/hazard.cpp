// Hazard Plugin
// Hold trigger key:
//   - If target < slashRange meters AND slash off cooldown → Violent Leap (Shift)
//   - Otherwise → shoot (Bonespur)

#include <xenon/SDK.hpp>
using namespace xenon;

static float    g_stiffness      = 30.f;
static float    g_slashStiffness = 0.f;   // 0 = instant snap to target when slashing
static float    g_fovRadius    = 200.f;
static int      g_triggerKey   = 1;     // set via keymap scanner
static float    g_slashRange   = 5.f;   // meters — leap if closer than this
static int32_t  g_cachedTarget = -1;
static bool     g_targetValid  = false;
static float    g_targetHp     = 0.f;
static float    g_targetDist   = 0.f;
static bool     g_slashReady   = false;
static bool     g_doingSlash   = false;
static bool     g_wasLeaping   = false;
static float    g_meleeDelay   = 0.f;   // countdown before pressing melee
static float    g_meleeHold    = 0.f;   // how long to hold melee button
static float    g_meleeDelaySet  = 0.15f;
static float    g_meleeHoldSet   = 0.10f;
static Vector2  g_targetScreen = { 0.f, 0.f };
static int      g_playerCount  = 0;
static bool     g_enabled      = true;
static bool     g_showHud      = true;
static bool     g_autoShoot    = true;
static bool     g_autoSlash    = true;
static bool     g_autoExec     = true;   // melee when target is close + low HP
static float    g_execDist     = 2.f;
static float    g_execHp       = 30.f;
static uint64_t g_heroId       = 0;    // unknown at compile time — use Lock checkbox
static bool     g_heroLock     = false;

XENON_PLUGIN_INFO(
    "hazard",
    "Hazard",
    "Xenon",
    "Aim assist: slash if close, shoot if far.",
    "1.0",
    0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

extern "C" void on_load()
{
    g_stiffness      = Config::GetFloat("stiffness",      30.f);
    g_slashStiffness = Config::GetFloat("slashStiffness", 0.f);
    g_fovRadius     = Config::GetFloat("fovRadius",     200.f);
    g_triggerKey    = Config::GetInt("triggerKey",      1);
    g_slashRange    = Config::GetFloat("slashRange",    5.f);
    g_meleeDelaySet = Config::GetFloat("meleeDelay",    0.15f);
    g_meleeHoldSet  = Config::GetFloat("meleeHold",     0.10f);
    g_enabled       = Config::GetBool("enabled",        true);
    g_showHud       = Config::GetBool("showHud",        true);
    g_autoShoot     = Config::GetBool("autoShoot",      true);
    g_autoSlash     = Config::GetBool("autoSlash",      true);
    g_autoExec      = Config::GetBool("autoExec",       true);
    g_execDist      = Config::GetFloat("execDist",      2.f);
    g_execHp        = Config::GetFloat("execHp",        30.f);
    uint32_t heroLo = (uint32_t)Config::GetInt("heroId_lo", 0);
    uint32_t heroHi = (uint32_t)Config::GetInt("heroId_hi", 0);
    g_heroId        = ((uint64_t)heroHi << 32) | heroLo;
    g_heroLock      = (g_heroId != 0);
}
extern "C" void on_unload()
{
    Config::SetFloat("stiffness",       g_stiffness);
    Config::SetFloat("slashStiffness",  g_slashStiffness);
    Config::SetFloat("fovRadius",  g_fovRadius);
    Config::SetInt("triggerKey",   g_triggerKey);
    Config::SetFloat("slashRange", g_slashRange);
    Config::SetFloat("meleeDelay", g_meleeDelaySet);
    Config::SetFloat("meleeHold",  g_meleeHoldSet);
    Config::SetBool("enabled",     g_enabled);
    Config::SetBool("showHud",     g_showHud);
    Config::SetBool("autoShoot",   g_autoShoot);
    Config::SetBool("autoSlash",   g_autoSlash);
    Config::SetBool("autoExec",    g_autoExec);
    Config::SetFloat("execDist",   g_execDist);
    Config::SetFloat("execHp",     g_execHp);
    Config::SetInt("heroId_lo", (int32_t)(g_heroId & 0xFFFFFFFF));
    Config::SetInt("heroId_hi", (int32_t)(g_heroId >> 32));
    Config::Save();
}
extern "C" void on_hero_changed(uint64_t) { g_cachedTarget = -1; AimResetSmoothing(); }

static float Sqrt(float v)
{
    if (v <= 0.f) return 0.f;
    float x = v * 0.5f;
    for (int i = 0; i < 8; i++) x = (x + v / x) * 0.5f;
    return x;
}
static float ScreenDist(Vector2 a, Vector2 b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return Sqrt(dx*dx + dy*dy);
}
static float WorldDist(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return Sqrt(dx*dx + dy*dy + dz*dz);
}

extern "C" void on_frame(float dt)
{
    (void)dt;

    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    if (!IsKeyDown(g_triggerKey) || g_cachedTarget < 0)
    {
        if (!IsKeyDown(g_triggerKey))
        {
            g_cachedTarget = -1;
            g_doingSlash   = false;
            AimResetSmoothing();
            ReleaseGameButton(GameButton::LMouse);
            ReleaseGameButton(GameButton::Skill1);
            ReleaseGameButton(GameButton::Melee);
            g_meleeDelay = 0.f;
            g_meleeHold  = 0.f;
            g_wasLeaping = false;
        }
        return;
    }

    // Get world distance to target
    float dist = 99999.f;
    Entity local = LocalPlayer();
    if (local.IsValid())
    {
        for (Entity p : Players())
        {
            if (p.Index() != g_cachedTarget) continue;
            dist = WorldDist(local.GetPosition(), p.GetPosition());
            break;
        }
    }

    g_targetDist = dist;

    bool leaping  = IsSkill1Active();
    bool inRange  = dist <= g_slashRange;
    g_slashReady  = !leaping;

    // Detect leap end → start melee delay only if within melee range
    if (g_wasLeaping && !leaping && dist <= 4.f)
        g_meleeDelay = g_meleeDelaySet;
    g_wasLeaping = leaping;

    // Exec melee — target close + low HP
    if (g_autoExec && g_meleeHold <= 0.f && g_meleeDelay <= 0.f
        && dist <= g_execDist && g_targetHp > 0.f && g_targetHp <= g_execHp)
        g_meleeHold = g_meleeHoldSet;

    // Melee delay countdown
    if (g_meleeDelay > 0.f)
    {
        g_meleeDelay -= dt;
        if (g_meleeDelay <= 0.f)
        {
            g_meleeDelay = 0.f;
            g_meleeHold  = g_meleeHoldSet;
        }
    }

    // Hold melee button — aim snap to chest handles facing
    if (g_meleeHold > 0.f)
    {
        PressGameButton(GameButton::Melee);
        g_meleeHold -= dt;
    }
    else
    {
        ReleaseGameButton(GameButton::Melee);
    }

    // Snap to chest during slash or melee; otherwise smooth aim at head
    bool slashing = g_autoSlash && leaping && inRange;
    bool meleeing = g_meleeHold > 0.f;
    int   aimBone      = (slashing || meleeing) ? Bone::Chest : Bone::Head;
    float aimStiffness = (slashing || meleeing) ? g_slashStiffness : g_stiffness;
    AimAtBone(g_cachedTarget, aimBone, aimStiffness);
    if (g_autoShoot && g_meleeHold <= 0.f && AimHitsHitbox(g_cachedTarget, 2.0f) >= 0)
        PressGameButton(GameButton::LMouse);
    else
        ReleaseGameButton(GameButton::LMouse);

    // Auto-slash — fires when leaping + in range + crosshair on target
    if (g_autoSlash && leaping && inRange && AimHitsHitbox(g_cachedTarget, 4.0f) >= 0)
    {
        PressGameButton(GameButton::Skill1);
        g_doingSlash = true;
    }
    else
    {
        ReleaseGameButton(GameButton::Skill1);
        g_doingSlash = false;
    }
}

extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    float cx = sz.x * 0.5f;
    float cy = sz.y * 0.5f;
    Vector2 center = { cx, cy };

    bool held = IsKeyDown(g_triggerKey);

    g_targetValid = false;
    g_playerCount = 0;

    int32_t bestIdx    = -1;
    float   bestDist   = 99999.f;
    float   bestHp     = 0.f;
    Vector2 bestScreen = { 0.f, 0.f };
    bool    lockedAlive  = false;
    float   lockedHp     = 0.f;
    Vector2 lockedScreen = { 0.f, 0.f };

    for (Entity p : Players())
    {
        g_playerCount++;
        if (!p.IsAlive() || p.IsLocal() || !p.IsVisible()) continue;

        Vector3 headWorld = p.GetHeadPos();
        if (!headWorld.IsValid()) continue;

        Vector2 headScreen;
        if (!WorldToScreen(headWorld, headScreen)) continue;

        Draw::CircleFilled(headScreen, 5.f, Color(255, 255, 0, 180));
        TextBuilder<16> htb;
        htb.put("P[").putInt(p.Index()).put("]");
        Draw::TextShadow(headScreen.x + 7.f, headScreen.y - 5.f, Color(255, 255, 0), htb.c_str(), 10);

        if (!held) continue;

        if (p.Index() == g_cachedTarget)
        {
            lockedAlive  = true;
            lockedHp     = p.GetHealth();
            lockedScreen = headScreen;
            continue;
        }

        float sd = ScreenDist(headScreen, center);
        if (sd > g_fovRadius) continue;
        if (sd < bestDist) { bestDist = sd; bestIdx = p.Index(); bestHp = p.GetHealth(); bestScreen = headScreen; }
    }

    if (held)
    {
        if (lockedAlive) { bestIdx = g_cachedTarget; bestHp = lockedHp; bestScreen = lockedScreen; }
        if (bestIdx >= 0)
        {
            if (bestIdx != g_cachedTarget) AimResetSmoothing();
            g_targetValid  = true;
            g_targetHp     = bestHp;
            g_targetScreen = bestScreen;
            g_cachedTarget = bestIdx;
        }
        else { g_cachedTarget = -1; }
    }
    else { g_cachedTarget = -1; }

    Draw::Circle(cx, cy, g_fovRadius, g_targetValid ? Color(0,255,0,120) : Color(255,255,255,60), 1.f);

    if (!g_showHud) return;

    if (g_targetValid)
    {
        float sx = g_targetScreen.x, sy = g_targetScreen.y;
        const float S = 8.f;
        Color c = g_doingSlash ? Color(255, 165, 0) : Color(255, 60, 60);
        Draw::Line(sx-S, sy, sx+S, sy, c, 2.f);
        Draw::Line(sx, sy-S, sx, sy+S, c, 2.f);
        Draw::Circle(sx, sy, 12.f, c, 1.5f);
    }

    // HUD
    const float DX = cx - 80.f;
    float dy = cy - 80.f;
    const int FONT = 14;
    const float LINE = 18.f;

    Draw::RectFilled(DX-4.f, dy-4.f, 165.f, LINE*10+8.f, Color(0,0,0,180));

    TextBuilder<48> tb;
    tb.put("KEY: ").put(held ? "DOWN" : "UP");
    Draw::Text(DX, dy, held ? Color::Green() : Color(160,160,160), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("TARGET: ").put(g_targetValid ? "FOUND" : "NONE");
    Draw::Text(DX, dy, g_targetValid ? Color::Green() : Color::Yellow(), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("HP: ").putFloat(g_targetHp, 0);
    Draw::Text(DX, dy, Color::White(), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("DIST: ").putFloat(g_targetDist, 1);
    Draw::Text(DX, dy, g_targetDist <= g_slashRange ? Color::Green() : Color::White(), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("SLASH RDY: ").put(g_slashReady ? "YES" : "NO");
    Draw::Text(DX, dy, g_slashReady ? Color::Green() : Color::Red(), tb.c_str(), FONT); dy += LINE;

    bool leaping = IsSkill1Active();
    tb.clear(); tb.put("LEAPING: ").put(leaping ? "YES" : "NO");
    Draw::Text(DX, dy, leaping ? Color::Green() : Color(160,160,160), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("IN RANGE: ").put(g_targetDist <= g_slashRange ? "YES" : "NO");
    Draw::Text(DX, dy, g_targetDist <= g_slashRange ? Color::Green() : Color(160,160,160), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("ACTION: ").put(g_doingSlash ? "SLASH" : "SHOOT");
    Draw::Text(DX, dy, g_doingSlash ? Color(255,165,0) : Color(255,60,60), tb.c_str(), FONT); dy += LINE;

    int hitbox = (g_cachedTarget >= 0) ? AimHitsHitbox(g_cachedTarget, 2.0f) : -1;
    tb.clear(); tb.put("HITBOX: ").putInt(hitbox);
    Draw::Text(DX, dy, hitbox >= 0 ? Color::Green() : Color::Red(), tb.c_str(), FONT);
}

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Hazard"))
    {
        ImGui::Checkbox("Enabled", &g_enabled);
        if (!g_enabled) return;
        ImGui::Checkbox("Show HUD",    &g_showHud);
        ImGui::Checkbox("Auto Shoot",  &g_autoShoot);
        ImGui::Checkbox("Auto Slash",  &g_autoSlash);
        ImGui::Checkbox("Auto Exec",   &g_autoExec);
        ImGui::Separator();
        ImGui::SliderFloat("Smoothing",        &g_stiffness,      0.f, 1500.f);
        ImGui::SliderFloat("Slash Smoothing",  &g_slashStiffness, 0.f, 1500.f);
        ImGui::SliderFloat("FOV Radius",  &g_fovRadius,  10.f,  500.f);
        ImGui::SliderInt("Trigger Key",   &g_triggerKey, 0,     31);
        ImGui::SliderFloat("Slash Range",   &g_slashRange,    1.f,  20.f);
        ImGui::SliderFloat("Melee Delay",   &g_meleeDelaySet, 0.f,  0.5f);
        ImGui::SliderFloat("Melee Hold",    &g_meleeHoldSet,  0.05f, 0.3f);
        ImGui::SliderFloat("Exec Dist (m)", &g_execDist,      0.5f,  5.f);
        ImGui::SliderFloat("Exec HP",       &g_execHp,        1.f,  200.f);
        ImGui::Separator();
        TextBuilder<48> hb;
        hb.put("Hero ID: ").putInt((int)g_heroId).put(g_heroId == 0 ? "  (any)" : "  (locked)");
        ImGui::Text(hb.c_str());
        bool prevLock = g_heroLock;
        ImGui::Checkbox("Lock to current hero", &g_heroLock);
        if (g_heroLock != prevLock)
        {
            if (g_heroLock) g_heroId = GetCurrentHero();
            else            g_heroId = 0;
        }
    }
}
