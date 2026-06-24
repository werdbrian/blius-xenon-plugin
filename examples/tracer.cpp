#include <xenon/SDK.hpp>
using namespace xenon;

static void PulseGameButton(uint32_t btn) { PressGameButton(btn); ReleaseGameButton(btn); }

XENON_PLUGIN_INFO(
    "tracerv2", "Tracer V2", "c", "", "2.1", HeroId::Tracer,
    PluginFlags::HasOverlay | PluginFlags::HasMenu | PluginFlags::HeroSpecific
)

// â”€â”€ Settings â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static bool   g_enabled           = true;
static bool   g_showDebugHud      = true;

static bool   g_aimbot            = false;
static float  g_aimbotSmoothing   = 800.f;
static bool   g_aimbotVisibleOnly = true;
static Hotkey g_aimbotKey(VK::XButton2);
static float  g_aimbotFovDeg      = 90.f;
static float  g_aimbotMaxDist     = 50.f;   // meters; 0 = unlimited
static bool   g_showFov           = true;
static Color  g_fovColor          = Color(255, 255, 255, 80);
static bool   g_targetLock        = false;

static bool   g_blinkAssist                = true;
static float  g_blinkAssistSmoothing       = 300.f;
static float  g_blinkAssistTimeout         = 0.5f;   // seconds to track after blink
static bool   g_blinkThroughMelee                = true;
static float  g_blinkThroughSmoothing            = 0.f;    // 0 = instant flip, higher = spring turn
static bool   g_blinkAssistRequireShooting        = true;

static bool   g_blinkEsp         = true;
static float  g_blinkEspMinDist  = 4.f;    // too close â€” no indicator
static float  g_blinkEspThruDist = 7.5f;   // below this = through-blink zone (turn + melee)
static float  g_blinkEspMaxDist  = 10.f;   // above this = out of range

// â”€â”€ Runtime state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static PluginEntity g_entities[32]{};
static int          g_entityCount = 0;
static Vector3      g_localPos{};
static int32_t      g_localTeam   = -1;

static int32_t  g_targetIdx     = -1;
static float    g_targetDist    = 0.f;
static uint32_t g_lockTargetId  = 0;    // 0 = no lock; set to e.id on key press

// â”€â”€ Blink assist state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//
//  Rising edge of IsSkill1Active() = blink fired.
//  BLINK_DETECT_DELAY after that, compare position delta to target direction:
//    dot(blink_vec, dir_to_target_now) < 0  â†’  through-blink (target behind us)
//    otherwise                               â†’  strafe/forward blink
//
//  Through:  instant snap every frame for g_blinkAssistTimeout + optional melee
//  Strafe:   spring back with g_blinkAssistSmoothing stiffness
//
static bool    g_wasSkill1Active      = false;
static bool    g_blinkAssistActive    = false;
static bool    g_blinkWasThrough      = false;
static bool    g_blinkHadStrafe       = false;   // A/D/S was held at blink time â†’ can't be through

static float   g_blinkAssistStart     = -999.f;
static float   g_blinkDetectAt        = -1.f;   // timestamp to evaluate blink direction
static Vector3 g_preBlinkPos          = {};      // position at blink start
static int32_t g_blinkAssistTargetIdx = -1;      // target locked at blink time

static bool    g_fireMeleeThisFrame   = false;

static constexpr float BLINK_DETECT_DELAY    = 0.08f;  // wait for g_localPos to settle post-blink

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Debug HUD
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static void DebugHud()
{
    if (!g_showDebugHud) return;

    float x = 10.f, y = 10.f;

    Draw::TextShadow(x, y, Color::White(), "[ TracerV2 Debug ]"); y += 18.f;

    Draw::TextShadow(x, y, IsSkill1Active() ? Color::Cyan()   : Color::White(),
        IsSkill1Active() ? "Blink:  ACTIVE" : "Blink:  ready"); y += 16.f;
    Draw::TextShadow(x, y, IsSkill2Active() ? Color::Green()  : Color::White(),
        IsSkill2Active() ? "Recall: ACTIVE" : "Recall: ready"); y += 16.f;
    Draw::TextShadow(x, y, IsUltReady() ? Color::Orange() : Color::White(),
        IsUltReady() ? "Ult:    READY" : "Ult:    charging"); y += 16.f;

    y += 6.f;

    if (g_blinkAssistActive)
    {
        if (g_blinkWasThrough)
            Draw::TextShadow(x, y, Color::Orange(), "Assist: THROUGH");
        else
            Draw::TextShadow(x, y, Color::Cyan(),   "Assist: SPRING");
    }
    else
    {
        Draw::TextShadow(x, y, Color(120, 120, 120, 200), "Assist: idle");
    }
    y += 18.f;

    if (g_targetLock)
    {
        if (g_lockTargetId != 0)
            Draw::TextShadow(x, y, Color::Orange(), "Lock:   ACTIVE");
        else
            Draw::TextShadow(x, y, Color(120, 120, 120, 200), "Lock:   waiting");
        y += 16.f;
    }

    Draw::TextShadow(x, y, Color::White(), "[ Best Target ]"); y += 18.f;

    if (g_targetIdx < 0 || g_targetIdx >= g_entityCount)
    {
        Draw::TextShadow(x, y, Color(160, 160, 160, 200), "No target");
        return;
    }

    const PluginEntity& t = g_entities[g_targetIdx];
    TextBuilder<64> tb;

    tb.put("Hero    ").put(GetHeroName(t.heroId));
    Draw::TextShadow(x, y, Color::White(), tb.c_str()); y += 16.f; tb.clear();

    float hpPct = t.maxHealth > 0.f ? t.health / t.maxHealth : 0.f;
    tb.put("HP      ").putInt((int)t.health).put(" / ").putInt((int)t.maxHealth);
    Draw::TextShadow(x, y, Color::HealthGradient(hpPct), tb.c_str()); y += 16.f; tb.clear();

    tb.put("Dist    ").putFloat(g_targetDist, 1).put(" m");
    Draw::TextShadow(x, y, Color::White(), tb.c_str()); y += 16.f; tb.clear();

    tb.put("Pos     ").putFloat(t.position.x, 1).put("  ")
                     .putFloat(t.position.y, 1).put("  ")
                     .putFloat(t.position.z, 1);
    Draw::TextShadow(x, y, Color(200, 200, 200, 200), tb.c_str());
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Lifecycle
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

extern "C" void on_load()
{
    g_enabled              = Config::GetBool("enabled",              true);
    g_showDebugHud         = Config::GetBool("showDebugHud",         true);
    g_aimbot               = Config::GetBool("aimbot",               false);
    g_aimbotSmoothing      = Config::GetFloat("aimbotSmoothing",     800.f);
    g_aimbotVisibleOnly    = Config::GetBool("aimbotVisibleOnly",    true);
    g_aimbotKey.Load("aimbotKey");
    g_aimbotFovDeg         = Config::GetFloat("aimbotFov",           90.f);
    g_aimbotMaxDist        = Config::GetFloat("aimbotMaxDist",       50.f);
    g_showFov              = Config::GetBool("showFov",              true);
    g_fovColor             = Config::GetColor("fovColor",            Color(255, 255, 255, 80));
    g_targetLock           = Config::GetBool("targetLock",           false);
    g_blinkAssist                = Config::GetBool("blinkAssist",                true);
    g_blinkAssistSmoothing       = Config::GetFloat("blinkAssistSmoothing",       300.f);
    g_blinkAssistTimeout         = Config::GetFloat("blinkAssistTimeout",          0.5f);
    g_blinkThroughMelee                = Config::GetBool("blinkThroughMelee",                true);
    g_blinkThroughSmoothing            = Config::GetFloat("blinkThroughSmoothing",           0.f);
    g_blinkAssistRequireShooting       = Config::GetBool("blinkAssistRequireShooting",        true);
    g_blinkEsp                   = Config::GetBool("blinkEsp",                   true);
    g_blinkEspMinDist            = Config::GetFloat("blinkEspMinDist",           4.f);
    g_blinkEspThruDist           = Config::GetFloat("blinkEspThruDist",          7.5f);
    g_blinkEspMaxDist            = Config::GetFloat("blinkEspMaxDist",           10.f);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",              g_enabled);
    Config::SetBool("showDebugHud",         g_showDebugHud);
    Config::SetBool("aimbot",               g_aimbot);
    Config::SetFloat("aimbotSmoothing",     g_aimbotSmoothing);
    Config::SetBool("aimbotVisibleOnly",    g_aimbotVisibleOnly);
    g_aimbotKey.Save("aimbotKey");
    Config::SetFloat("aimbotFov",           g_aimbotFovDeg);
    Config::SetFloat("aimbotMaxDist",       g_aimbotMaxDist);
    Config::SetBool("showFov",              g_showFov);
    Config::SetColor("fovColor",            g_fovColor);
    Config::SetBool("targetLock",           g_targetLock);
    Config::SetBool("blinkAssist",                g_blinkAssist);
    Config::SetFloat("blinkAssistSmoothing",      g_blinkAssistSmoothing);
    Config::SetFloat("blinkAssistTimeout",        g_blinkAssistTimeout);
    Config::SetBool("blinkThroughMelee",                g_blinkThroughMelee);
    Config::SetFloat("blinkThroughSmoothing",           g_blinkThroughSmoothing);
    Config::SetBool("blinkAssistRequireShooting",       g_blinkAssistRequireShooting);
    Config::SetBool("blinkEsp",                   g_blinkEsp);
    Config::SetFloat("blinkEspMinDist",           g_blinkEspMinDist);
    Config::SetFloat("blinkEspThruDist",          g_blinkEspThruDist);
    Config::SetFloat("blinkEspMaxDist",           g_blinkEspMaxDist);
    Config::Save();
}

extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled",   &g_enabled);
    ImGui::Checkbox("Debug HUD", &g_showDebugHud);
    ImGui::Separator();

    ImGui::Checkbox("Aimbot", &g_aimbot);
    if (g_aimbot)
    {
        g_aimbotKey.Render("Aimbot Key");
        ImGui::SliderFloat("Smoothing",     &g_aimbotSmoothing,   0.f, 1500.f);
        ImGui::Checkbox("Visible Only",     &g_aimbotVisibleOnly);
        ImGui::Checkbox("Target Lock",      &g_targetLock);
        ImGui::SliderFloat("FOV (degrees)", &g_aimbotFovDeg,   0.f, 360.f);
        ImGui::SliderFloat("Max Dist (m)",  &g_aimbotMaxDist,  0.f, 100.f);
        ImGui::Checkbox("Show FOV",         &g_showFov);
        if (g_showFov)
            ImGui::ColorPicker("FOV Color", &g_fovColor);
    }
    ImGui::Separator();

    ImGui::Checkbox("Blink Assist", &g_blinkAssist);
    if (g_blinkAssist)
    {
        ImGui::SliderFloat("Blink Smoothing",  &g_blinkAssistSmoothing,  0.f, 1500.f);
        ImGui::SliderFloat("Turn Smoothing",   &g_blinkThroughSmoothing, 0.f, 1500.f);
        ImGui::SliderFloat("Track Timeout (s)", &g_blinkAssistTimeout,   0.1f, 2.f);
        ImGui::Checkbox("Through-blink Melee", &g_blinkThroughMelee);
        ImGui::Checkbox("Require Shooting",    &g_blinkAssistRequireShooting);
    }
    ImGui::Separator();

    ImGui::Checkbox("Blink ESP", &g_blinkEsp);
    if (g_blinkEsp)
    {
        ImGui::SliderFloat("Min dist (m)",  &g_blinkEspMinDist,  0.f, 5.f);
        if (g_blinkEspMinDist >= g_blinkEspThruDist) g_blinkEspThruDist = g_blinkEspMinDist + 0.5f;
        ImGui::SliderFloat("Thru dist (m)", &g_blinkEspThruDist, g_blinkEspMinDist + 0.5f, 15.f);
        if (g_blinkEspThruDist >= g_blinkEspMaxDist) g_blinkEspMaxDist = g_blinkEspThruDist + 0.5f;
        ImGui::SliderFloat("Max dist (m)",  &g_blinkEspMaxDist,  g_blinkEspThruDist + 0.5f, 20.f);
    }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  on_frame â€” entity snapshot + melee pulse
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

extern "C" void on_frame(float)
{
    g_aimbotKey.Update();

    int count = GetPlayerCount();
    g_entityCount = count < 32 ? count : 32;
    for (int i = 0; i < g_entityCount; i++)
    {
        xn_get_entity(i, &g_entities[i]);
        if (g_entities[i].isLocalPlayer)
        {
            g_localPos  = g_entities[i].position;
            g_localTeam = (int32_t)g_entities[i].team;
        }
    }

    if (g_targetIdx >= 0 && g_targetIdx < g_entityCount)
        g_targetDist = xn_get_distance_to_entity(g_targetIdx);

    if (g_fireMeleeThisFrame)
    {
        PulseGameButton(GameButton::Melee);
        g_fireMeleeThisFrame = false;
    }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  on_render â€” targeting + blink assist + draw
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

extern "C" void on_render()
{
    if (!g_enabled) return;

    float now   = GetTime();
    float fovPx = (g_aimbotFovDeg / 360.f) * ScreenSize().x;

    // â”€â”€ Target finding â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    int flags = TargetFlags::Enemy;
    if (g_aimbotVisibleOnly) flags |= TargetFlags::Visible;
    int32_t rawIdx = xn_find_best_target_fov(fovPx, Bone::Head, flags);

    static int32_t s_prevTargetIdx = -1;
    static bool    s_wasKeyDown    = false;
    int32_t newIdx = (rawIdx >= 0 && rawIdx != (int32_t)0xFFFFFFFF) ? rawIdx : -1;

    bool keyDown = g_aimbot && g_aimbotKey.IsDown();

    if (g_targetLock)
    {
        // Rising edge of key with a valid FOV target â†’ lock onto them
        if (keyDown && !s_wasKeyDown && newIdx >= 0)
        {
            g_lockTargetId = g_entities[newIdx].id;
            AimResetSmoothing();
            s_prevTargetIdx = newIdx;
        }
        if (!keyDown)
            g_lockTargetId = 0;
    }
    s_wasKeyDown = keyDown;

    if (g_targetLock && keyDown && g_lockTargetId != 0)
    {
        // Find locked entity by stable ID â€” always track regardless of FOV so
        // blink detection captures g_targetIdx correctly at the blink frame
        int32_t lockedIdx = -1;
        for (int i = 0; i < g_entityCount; i++)
        {
            if (g_entities[i].id == g_lockTargetId) { lockedIdx = i; break; }
        }

        if (lockedIdx >= 0 && !g_entities[lockedIdx].alive)
        {
            g_lockTargetId = 0;
            lockedIdx = -1;
        }

        if (lockedIdx != s_prevTargetIdx) { AimResetSmoothing(); s_prevTargetIdx = lockedIdx; }
        g_targetIdx = lockedIdx;
    }
    else
    {
        if (newIdx != s_prevTargetIdx) { AimResetSmoothing(); s_prevTargetIdx = newIdx; }
        g_targetIdx = newIdx;
    }

    // â”€â”€ Aimbot â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // When target lock is active, pause aim if locked target leaves the FOV
    // circle â€” blink assist still fires via its own g_blinkAssistTargetIdx.
    bool aimAllowed = true;
    if (g_targetLock && g_lockTargetId != 0 && g_targetIdx >= 0 && g_targetIdx < g_entityCount)
    {
        const PluginEntity& lt = g_entities[g_targetIdx];
        Vector3 headW = { lt.position.x, lt.position.y, lt.position.z + lt.delta2.z };
        Vector2 headS;
        Vector2 ctr = ScreenCenter();
        if (WorldToScreen(headW, headS))
        {
            float fx = headS.x - ctr.x;
            float fy = headS.y - ctr.y;
            aimAllowed = (fx*fx + fy*fy) <= (fovPx * fovPx);
        }
        else
        {
            aimAllowed = false;  // off-screen means target is behind us
        }
    }

    if (aimAllowed && g_aimbot && g_aimbotKey.IsDown()
        && g_targetIdx >= 0 && g_targetIdx < g_entityCount
        && g_entities[g_targetIdx].alive
        && (g_aimbotMaxDist <= 0.f || g_targetDist <= g_aimbotMaxDist))
    {
        AimAtBone(g_targetIdx, Bone::Head, g_aimbotSmoothing);
    }

    // â”€â”€ Blink detection â€” rising edge of IsSkill1Active â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    bool skill1Now = IsSkill1Active();

    if (g_blinkAssist && skill1Now && !g_wasSkill1Active)
    {
        // Always activate on any blink if a target exists.
        // Shooting gates are checked per-feature below, not here.
        if (g_targetIdx >= 0 && g_targetIdx < g_entityCount
            && g_entities[g_targetIdx].alive)
        {
            g_blinkAssistActive    = true;
            g_blinkWasThrough      = false;
            g_blinkHadStrafe       = IsKeyDown(0x41)   // A
                                  || IsKeyDown(0x44)   // D
                                  || IsKeyDown(0x53);  // S

            g_blinkAssistStart     = now;
            g_blinkDetectAt        = now + BLINK_DETECT_DELAY;
            g_preBlinkPos          = g_localPos;
            g_blinkAssistTargetIdx = g_targetIdx;
        }
    }

    g_wasSkill1Active = skill1Now;

    // â”€â”€ Through-blink direction check (fires once, after position settles) â”€â”€â”€â”€
    //
    //  blink_vec = current pos - pre-blink pos  (where we moved)
    //  dir_to_target = target pos - current pos  (where target is now)
    //
    //  dot(blink_vec, dir_to_target) < 0  â†’  target is behind our blink path
    //                                       = we flew through them
    //
    if (g_blinkAssistActive && !g_blinkWasThrough
        && g_blinkDetectAt > 0.f && now >= g_blinkDetectAt)
    {
        g_blinkDetectAt = -1.f;  // one-shot

        float bx = g_localPos.x - g_preBlinkPos.x;
        float by = g_localPos.y - g_preBlinkPos.y;
        float bz = g_localPos.z - g_preBlinkPos.z;
        float blinkDistSq = bx*bx + by*by + bz*bz;

        if (blinkDistSq > 0.5f * 0.5f
            && g_blinkAssistTargetIdx >= 0
            && g_blinkAssistTargetIdx < g_entityCount)
        {
            const PluginEntity& t = g_entities[g_blinkAssistTargetIdx];

            // Vector from pre-blink position to target (was target in our path?)
            float px = t.position.x - g_preBlinkPos.x;
            float py = t.position.y - g_preBlinkPos.y;
            float pz = t.position.z - g_preBlinkPos.z;

            // Vector from post-blink position to target (is target behind us now?)
            float tx = t.position.x - g_localPos.x;
            float ty = t.position.y - g_localPos.y;
            float tz = t.position.z - g_localPos.z;

            // Through-blink requires BOTH:
            //   1. we were moving toward the target (pre-blink target was ahead)
            //   2. target is now behind our blink direction (we overshot)
            // Strafe blinks fail condition 1 â€” target was never in the blink path.
            bool movedTowardTarget = (px*bx + py*by + pz*bz) > 0.f;
            bool targetNowBehind   = (tx*bx + ty*by + tz*bz) < 0.f;

            if (!g_blinkHadStrafe && movedTowardTarget && targetNowBehind)
            {
                g_blinkWasThrough = true;
                AimResetSmoothing();
                AimAtBone(g_blinkAssistTargetIdx, Bone::Head, g_blinkThroughSmoothing);
                float postBlinkDistSq = tx*tx + ty*ty + tz*tz;
                if (g_blinkThroughMelee && postBlinkDistSq <= 3.f * 3.f)
                    g_fireMeleeThisFrame = true;
            }
        }
    }

    // â”€â”€ Blink assist tracking â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    //
    //  Through-blink: stiffness 0 (instant snap) â€” holds aim on target for
    //    the full correction window so the follow-up shot/melee lands.
    //  Strafe/forward blink: configured smoothing springs aim back to target.
    //
    if (g_blinkAssistActive)
    {
        bool expired   = now - g_blinkAssistStart >= g_blinkAssistTimeout;
        bool noTarget  = g_blinkAssistTargetIdx < 0
                      || g_blinkAssistTargetIdx >= g_entityCount
                      || !g_entities[g_blinkAssistTargetIdx].alive;

        if (expired || noTarget)
        {
            g_blinkAssistActive = false;
        }
        else if (g_blinkWasThrough)
        {
            // Through-blink turn: always applies, no shooting gate
            AimAtBone(g_blinkAssistTargetIdx, Bone::Head, g_blinkThroughSmoothing);
        }
        else if (!g_blinkAssistRequireShooting || IsKeyDown(0x01))
        {
            // Strafe/forward spring-back: gated by Require Shooting option
            AimAtBone(g_blinkAssistTargetIdx, Bone::Head, g_blinkAssistSmoothing);
        }
    }

    DebugHud();

    // â”€â”€ Entity dots + blink ESP â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    for (int i = 0; i < g_entityCount; i++)
    {
        const PluginEntity& e = g_entities[i];
        if (!e.alive || e.isLocalPlayer) continue;

        Vector2 screen;
        if (!WorldToScreen(e.position, screen)) continue;

        if (!g_blinkEsp || (int32_t)e.team == g_localTeam) continue;

        float dx = e.position.x - g_localPos.x;
        float dy = e.position.y - g_localPos.y;
        float dz = e.position.z - g_localPos.z;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq < g_blinkEspMinDist  * g_blinkEspMinDist) continue;  // too close
        if (distSq > g_blinkEspMaxDist  * g_blinkEspMaxDist) continue;  // out of range

        float dist = sqrtf(distSq);
        TextBuilder<32> tb;

        if (distSq < g_blinkEspThruDist * g_blinkEspThruDist)
        {
            // Through-blink zone: blink past them, need to turn and melee
            tb.put("TURN + MELEE  ").putFloat(dist, 1).put("m");
            Draw::TextShadow(screen.x - 40.f, screen.y - 26.f,
                Color(255, 120, 30, 240), tb.c_str());
        }
        else
        {
            // Toward-blink zone: blink to them, melee on arrival
            tb.put("BLINK MELEE  ").putFloat(dist, 1).put("m");
            Draw::TextShadow(screen.x - 40.f, screen.y - 26.f,
                Color(255, 220, 0, 220), tb.c_str());
        }
    }

    if (g_aimbot && g_showFov && g_aimbotFovDeg > 0.f && g_aimbotFovDeg < 360.f)
        Draw::Circle(ScreenCenter(), fovPx, g_fovColor, 1.f);
}

