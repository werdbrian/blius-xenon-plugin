// Widowmaker Plugin
// Target selection happens in on_render (where WorldToScreen is valid).
// on_frame reads the cached target and calls AimAtBone.

#include <xenon/SDK.hpp>
using namespace xenon;

// Target mode constants
namespace TargetMode {
    constexpr int ClosestCrosshair = 0;  // smallest screen-space distance from center
    constexpr int LowestHP         = 1;  // least HP first
    constexpr int HighestHP        = 2;  // most HP first
    constexpr int ClosestWorld     = 3;  // nearest in 3D world distance
    constexpr int Priority         = 4;  // Mercy > supports (low HP) > others
}

static float    g_stiffness       = 30.f;
static float    g_fovRadius       = 150.f;
static int      g_targetMode      = TargetMode::ClosestCrosshair;
static bool     g_triggerBot      = false;
static Hotkey   g_triggerKey(0);  // unbound (vk 0) = always on; click-to-bind in menu
static float    g_chargeThreshold = 1.0f;
static int32_t  g_cachedTarget    = -1;
static bool     g_targetValid     = false;
static float    g_targetHp        = 0.f;
static Vector2  g_targetScreen    = { 0.f, 0.f };
static int      g_playerCount     = 0;
static bool     g_enabled         = true;
static uint64_t g_heroId          = HeroId::Widowmaker;
static bool     g_heroLock        = true;

XENON_PLUGIN_INFO(
    "widowmaker",
    "Widowmaker",
    "Xenon",
    "Aim assist: configurable target selection, sticky lock, RClick trigger.",
    "1.0",
    0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

extern "C" void on_load()
{
    g_stiffness       = Config::GetFloat("stiffness", 30.f);
    g_fovRadius       = Config::GetFloat("fovRadius", 150.f);
    g_targetMode      = Config::GetInt("targetMode", 0);
    g_triggerBot      = Config::GetBool("triggerBot", false);
    g_triggerKey.Load("triggerKey");
    g_chargeThreshold = Config::GetFloat("chargeThreshold2", 1.0f);
    g_enabled         = Config::GetBool("enabled", true);
    uint32_t heroLo   = (uint32_t)Config::GetInt("heroId_lo", (int32_t)(HeroId::Widowmaker & 0xFFFFFFFF));
    uint32_t heroHi   = (uint32_t)Config::GetInt("heroId_hi", (int32_t)(HeroId::Widowmaker >> 32));
    g_heroId          = ((uint64_t)heroHi << 32) | heroLo;
    g_heroLock        = (g_heroId != 0);
}
extern "C" void on_unload()
{
    Config::SetFloat("stiffness",       g_stiffness);
    Config::SetFloat("fovRadius",       g_fovRadius);
    Config::SetInt("targetMode",        g_targetMode);
    Config::SetBool("triggerBot",       g_triggerBot);
    g_triggerKey.Save("triggerKey");
    Config::SetFloat("chargeThreshold2", g_chargeThreshold);
    Config::SetBool("enabled",          g_enabled);
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
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return Sqrt(dx*dx + dy*dy);
}

static float WorldDist(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return Sqrt(dx*dx + dy*dy + dz*dz);
}

// Returns score for a candidate — lower score = better target
static float ScoreCandidate(Entity p, Vector2 headScreen, Vector2 center, Vector3 localPos)
{
    switch (g_targetMode)
    {
        case TargetMode::LowestHP:
            return p.GetHealth();

        case TargetMode::HighestHP:
            return -p.GetHealth();

        case TargetMode::ClosestWorld:
            return WorldDist(p.GetPosition(), localPos);

        case TargetMode::Priority:
        {
            // Mercy always first, then by lowest HP
            if (p.GetHeroId() == HeroId::Mercy) return -99999.f;
            return p.GetHealth();
        }

        case TargetMode::ClosestCrosshair:
        default:
            return ScreenDist(headScreen, center);
    }
}

// on_frame: read cached target and call AimAtBone
extern "C" void on_frame(float dt)
{
    (void)dt;

    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    g_triggerKey.Update();

    if (!IsKeyDown(1) || g_cachedTarget < 0)
    {
        if (!IsKeyDown(1))
        {
            g_cachedTarget = -1;
            AimResetSmoothing();
            ReleaseGameButton(GameButton::ScopedShoot);
        }
        return;
    }

    AimAtBone(g_cachedTarget, Bone::Head, g_stiffness);

    if (g_triggerBot)
    {
        bool keyHeld = (g_triggerKey.vk == 0) || g_triggerKey.IsDown();
        float charge = GetWidowCharge();
        if (keyHeld && charge >= g_chargeThreshold && AimHitsHitbox(g_cachedTarget, 1.0f) >= 0)
            PressGameButton(GameButton::ScopedShoot);
        else
            ReleaseGameButton(GameButton::ScopedShoot);
    }
}

// on_render: W2S valid here — do target selection and draw HUD
extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    float cx = sz.x * 0.5f;
    float cy = sz.y * 0.5f;
    Vector2 center = { cx, cy };

    bool rclick = IsKeyDown(1);

    // Get local position for world-distance mode
    Vector3 localPos = { 0.f, 0.f, 0.f };
    Entity local = LocalPlayer();
    if (local.IsValid()) localPos = local.GetPosition();

    // --- Target selection ---
    g_targetValid = false;
    g_playerCount = 0;

    int32_t bestIdx    = -1;
    float   bestScore  = 99999.f;
    float   bestHp     = 0.f;
    Vector2 bestScreen = { 0.f, 0.f };

    for (Entity p : Players())
    {
        g_playerCount++;
        if (!p.IsAlive() || p.IsLocal() || !p.IsVisible()) continue;

        Vector3 headWorld = p.GetHeadPos();
        if (!headWorld.IsValid()) continue;

        Vector2 headScreen;
        if (!WorldToScreen(headWorld, headScreen)) continue;

        // Yellow dot on all visible players
        Draw::CircleFilled(headScreen, 5.f, Color(255, 255, 0, 180));
        TextBuilder<16> htb;
        htb.put("P[").putInt(p.Index()).put("]");
        Draw::TextShadow(headScreen.x + 7.f, headScreen.y - 5.f, Color(255, 255, 0), htb.c_str(), 10);

        if (!rclick) continue;

        // Must be within FOV circle
        float screenDist = ScreenDist(headScreen, center);
        if (screenDist > g_fovRadius) continue;

        float score = ScoreCandidate(p, headScreen, center, localPos);
        if (score < bestScore)
        {
            bestScore  = score;
            bestIdx    = p.Index();
            bestHp     = p.GetHealth();
            bestScreen = headScreen;
        }
    }

    if (rclick)
    {
        if (bestIdx >= 0)
        {
            if (bestIdx != g_cachedTarget) AimResetSmoothing();
            g_targetValid  = true;
            g_targetHp     = bestHp;
            g_targetScreen = bestScreen;
            g_cachedTarget = bestIdx;
        }
        else
        {
            g_cachedTarget = -1;
        }
    }
    else
    {
        g_cachedTarget = -1;
    }

    // --- HUD ---
    Color fovColor = g_targetValid ? Color(0, 255, 0, 120) : Color(255, 255, 255, 60);
    Draw::Circle(cx, cy, g_fovRadius, fovColor, 1.f);

    const float DX   = cx - 80.f;
    float       dy   = cy - 80.f;
    const int   FONT = 14;
    const float LINE = 18.f;

    Draw::RectFilled(DX - 4.f, dy - 4.f, 165.f, LINE * 6 + 8.f, Color(0, 0, 0, 180));

    TextBuilder<48> tb;
    tb.put("RCLICK: ").put(rclick ? "DOWN" : "UP");
    Draw::Text(DX, dy, rclick ? Color::Green() : Color(160,160,160), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("TARGET: ").put(g_targetValid ? "FOUND" : "NONE");
    Draw::Text(DX, dy, g_targetValid ? Color::Green() : Color::Yellow(), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("HP: ").putFloat(g_targetHp, 0);
    Draw::Text(DX, dy, Color::White(), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("PLAYERS: ").putInt(g_playerCount);
    Draw::Text(DX, dy, Color::White(), tb.c_str(), FONT); dy += LINE;

    tb.clear(); tb.put("IDX: ").putInt(g_cachedTarget);
    Draw::Text(DX, dy, Color::White(), tb.c_str(), FONT); dy += LINE;

    float charge = GetWidowCharge();
    tb.clear(); tb.put("CHARGE: ").putFloat(charge, 2);
    Draw::Text(DX, dy, charge >= g_chargeThreshold ? Color::Green() : Color::Yellow(), tb.c_str(), FONT);

    // Red crosshair on selected target
    if (g_targetValid)
    {
        float sx = g_targetScreen.x;
        float sy = g_targetScreen.y;
        const float S = 8.f;
        Draw::Line(sx - S, sy, sx + S, sy, Color(255, 60, 60), 2.f);
        Draw::Line(sx, sy - S, sx, sy + S, Color(255, 60, 60), 2.f);
        Draw::Circle(sx, sy, 12.f, Color(255, 60, 60, 200), 1.5f);
    }
}

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Widowmaker"))
    {
        ImGui::Checkbox("Enabled", &g_enabled);
        if (!g_enabled) return;
        ImGui::Separator();
        ImGui::SliderFloat("Smoothing", &g_stiffness, 0.f, 1500.f);
        ImGui::SliderFloat("FOV Radius", &g_fovRadius, 10.f, 500.f);
        ImGui::Combo("Target Mode", &g_targetMode,
            "Closest Crosshair\0Lowest HP\0Highest HP\0Closest World\0Priority (Mercy first)\0");
        ImGui::Separator();
        ImGui::Checkbox("Trigger Bot", &g_triggerBot);
        if (g_triggerBot)
        {
            g_triggerKey.Render("Trigger Key (unbound = always)");
            ImGui::SliderFloat("Charge Threshold", &g_chargeThreshold, 0.f, 1.0f);
        }
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
