#include <xenon/SDK.hpp>
using namespace xenon;

XENON_PLUGIN_INFO(
    "hogv1", "Roadhog V1", "c", "", "1.0", HeroId::Roadhog,
    PluginFlags::HasOverlay | PluginFlags::HasMenu | PluginFlags::HeroSpecific
)

// ── Settings ──────────────────────────────────────────────────────────────────
static bool  g_enabled      = true;
static bool  g_showDebugHud = false;

// Hook aimbot
static bool  g_hookAimbot   = true;
static float g_fovDeg       = 90.f;
static bool  g_showFov      = true;
static Color g_fovColor     = Color(255, 140, 40, 80);
static float g_smoothing    = 0.f;
static bool  g_visOnly      = false;
static float g_maxDist      = 20.f;
static float g_flickTime    = 0.35f;

enum { AB_HEAD = 0, AB_NECK, AB_CHEST, AB_CLOSEST };
static const char* kAimBones = "Head\0Neck\0Chest\0Closest\0";
static int   g_aimBone      = AB_HEAD;

// Hook indicator
static bool  g_showHookIndicator = true;
static Color g_hookIndicatorColor = Color(255, 210, 50, 255);
static float g_indicatorPadX     = 0.f;
static float g_indicatorPadY     = -22.f;

// Auto Take a Breather
static bool  g_autoBreather    = true;
static float g_breatherHpPct   = 90.f;

// Auto shoot
static bool  g_autoShoot       = true;
static Hotkey g_shootKey(18);  // click-to-bind in menu (VK_MENU = Left Alt)
static float g_shootFovDeg     = 90.f;
static bool  g_showShootFov    = true;
static Color g_shootFovColor   = Color(100, 220, 100, 60);
static float g_shootSmoothing  = 30.f;
static float g_hitboxScale     = 1.0f;

// Combo
static bool  g_comboEnabled    = false;
static float g_pullTriggerDist = 3.75f;   // fire combo when target closer than this (m)
static float g_pullVelThresh   = 0.08f; // min approach per frame to count as pull (~5 m/s at 60fps)
static float g_comboTimeout    = 2.f;   // abort combo if no pull detected within this time
static float g_shootDelay      = 0.13f;
static float g_meleeDelay      = 0.1f;

// ── Runtime state ─────────────────────────────────────────────────────────────
static PluginEntity g_entities[32]{};
static int          g_entityCount = 0;
static Vector3      g_localPos{};
static int32_t      g_localTeam   = -1;
static float        g_localHp     = 0.f;
static float        g_localMaxHp  = 0.f;

//  HA_IDLE        — waiting for hook
//  HA_FLICK       — hook thrown, snapping aim for g_flickTime
//  HA_COMBO_WAIT  — watching target distance/velocity for pull detection
//  HA_SHOOT       — pull detected + target close, re-aim + fire primary
//  HA_MELEE       — wait melee delay, fire melee
enum HookAimPhase { HA_IDLE = 0, HA_FLICK, HA_COMBO_WAIT, HA_SHOOT, HA_MELEE };
static HookAimPhase g_haPhase      = HA_IDLE;
static float        g_phaseAt      = 0.f;
static bool         g_hookPrev     = false;
static uint32_t     g_targetId     = 0;
static int32_t      g_targetIdx    = -1;
static Vector3      g_targetPos    = {};
static float        g_targetDist   = 0.f;

// Pull detection state
static bool  g_pullDetected = false;
static float g_prevDist     = 0.f;

// Auto shoot state (cached from on_render)
static int32_t g_shootTargetIdx   = -1;
static bool    g_shootTargetValid = false;
static int     g_shootHitbox      = -1;
static float   g_shootTargetDist  = 0.f;

// RMouse range
static float   g_rmbMinDist       = 15.f;
static float   g_rmbMaxDist       = 30.f;

// Combo button hold timers
static float g_shootHoldEnd = 0.f;
static float g_meleeHoldEnd = 0.f;
static constexpr float kComboHoldDur = 0.12f;

// ─────────────────────────────────────────────
//  Aim helpers
// ─────────────────────────────────────────────

static int ResolveAimBoneId(int32_t idx)
{
    if (g_aimBone == AB_NECK)  return Bone::Neck;
    if (g_aimBone == AB_CHEST) return Bone::Chest;
    if (g_aimBone == AB_CLOSEST)
    {
        static const int kCandidates[] = { Bone::Head, Bone::Neck, Bone::Chest };
        Vector2 ctr  = ScreenCenter();
        int   best   = Bone::Head;
        float bestSq = 1e30f;
        for (int i = 0; i < 3; ++i)
        {
            Vector3 wp; Vector2 sp;
            xn_get_bone_pos(idx, kCandidates[i], &wp);
            if (WorldToScreen(wp, sp))
            {
                float dx = sp.x - ctr.x, dy = sp.y - ctr.y;
                float dsq = dx*dx + dy*dy;
                if (dsq < bestSq) { bestSq = dsq; best = kCandidates[i]; }
            }
        }
        return best;
    }
    return Bone::Head;
}

static void AimAtTarget()
{
    if (g_targetIdx < 0 || g_targetIdx >= g_entityCount)
    {
        AimAtPosition(g_targetPos, g_smoothing);
        return;
    }
    AimAtBone(g_targetIdx, ResolveAimBoneId(g_targetIdx), g_smoothing);
}

// ─────────────────────────────────────────────
//  Debug HUD
// ─────────────────────────────────────────────

static void DebugHud()
{
    if (!g_showDebugHud) return;

    float x = 10.f, y = 10.f;
    TextBuilder<64> tb;

    Draw::TextShadow(x, y, Color::White(), "[ Roadhog V1 ]"); y += 18.f;

    SkillCooldown s1 = GetSkill1Cooldown();
    bool hookActive  = IsSkill1Active();

    if (hookActive) {
        Draw::TextShadow(x, y, Color::Cyan(), "Hook:  ACTIVE"); y += 16.f;
    } else if (s1.current > 0.f) {
        tb.put("Hook:  ").putFloat(s1.current, 1).put("s");
        Draw::TextShadow(x, y, Color(160, 160, 160, 200), tb.c_str()); y += 16.f; tb.clear();
    } else {
        Draw::TextShadow(x, y, Color::Green(), "Hook:  READY"); y += 16.f;
    }

    // Raw skill1 values to diagnose enabled/current issues
    tb.put("s1 en=").putInt(s1.enabled ? 1 : 0).put(" cur=").putFloat(s1.current, 2);
    Draw::TextShadow(x, y, Color(180, 180, 180, 160), tb.c_str()); y += 14.f; tb.clear();

    SkillCooldown s2 = GetSkill2Cooldown();
    if (IsSkill2Active()) {
        Draw::TextShadow(x, y, Color::Cyan(), "Breather: ACTIVE"); y += 16.f;
    } else if (s2.IsOnCooldown()) {
        tb.put("Breather: ").putFloat(s2.current, 1).put("s");
        Draw::TextShadow(x, y, Color(160, 160, 160, 200), tb.c_str()); y += 16.f; tb.clear();
    } else {
        Draw::TextShadow(x, y, Color::Green(), "Breather: READY"); y += 16.f;
    }

    if (g_localMaxHp > 0.f)
    {
        float hp = g_localHp / g_localMaxHp * 100.f;
        tb.put("My HP: ").putFloat(hp, 0).put("%");
        Color hpCol = (g_autoBreather && hp < g_breatherHpPct) ? Color::Red() : Color::Green();
        Draw::TextShadow(x, y, hpCol, tb.c_str()); y += 16.f; tb.clear();
    }

    y += 4.f;

    static const char* kPhaseNames[] = { "idle", "FLICK", "WAIT", "SHOOT", "MELEE" };
    Color phaseCol = g_haPhase == HA_IDLE       ? Color(120, 120, 120, 200)
                   : g_haPhase == HA_FLICK      ? Color::Orange()
                   : g_haPhase == HA_COMBO_WAIT ? Color::Yellow()
                   :                              Color::Red();
    tb.put("Phase: ").put(kPhaseNames[(int)g_haPhase]);
    Draw::TextShadow(x, y, phaseCol, tb.c_str()); y += 16.f; tb.clear();

    // Pull detection debug
    if (g_haPhase == HA_COMBO_WAIT)
    {
        tb.put("Pull:  ").put(g_pullDetected ? "YES" : "watching");
        tb.put("  dist=").putFloat(g_targetDist, 1);
        Draw::TextShadow(x, y, g_pullDetected ? Color::Green() : Color::Yellow(),
                         tb.c_str()); y += 16.f; tb.clear();
    }

    if (g_targetIdx >= 0 && g_targetIdx < g_entityCount)
    {
        y += 4.f;
        Draw::TextShadow(x, y, Color::White(), "[ Target ]"); y += 18.f;

        const PluginEntity& t = g_entities[g_targetIdx];
        float hpPct = t.maxHealth > 0.f ? t.health / t.maxHealth : 0.f;

        tb.put("Hero   ").put(GetHeroName(t.heroId));
        Draw::TextShadow(x, y, Color::White(), tb.c_str()); y += 16.f; tb.clear();

        tb.put("HP     ").putInt((int)t.health).put(" / ").putInt((int)t.maxHealth);
        Draw::TextShadow(x, y, Color::HealthGradient(hpPct), tb.c_str()); y += 16.f; tb.clear();

        tb.put("Dist   ").putFloat(g_targetDist, 1).put(" m");
        Draw::TextShadow(x, y, Color::White(), tb.c_str());
    }
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────

extern "C" void on_load()
{
    g_enabled          = Config::GetBool("enabled",          true);
    g_showDebugHud     = Config::GetBool("showDebugHud",     false);
    g_hookAimbot       = Config::GetBool("hookAimbot",       true);
    g_fovDeg           = Config::GetFloat("fovDeg",          90.f);
    g_showFov          = Config::GetBool("showFov",          true);
    g_fovColor         = Config::GetColor("fovColor",        Color(255, 140, 40, 80));
    g_smoothing        = Config::GetFloat("smoothing",       0.f);
    g_visOnly          = Config::GetBool("visOnly",          false);
    g_maxDist          = Config::GetFloat("maxDist",         20.f);
    g_flickTime        = Config::GetFloat("flickTime",       0.35f);
    g_aimBone          = (int)Config::GetFloat("aimBone",    0.f);
    g_showHookIndicator  = Config::GetBool("showHookIndicator",   true);
    g_hookIndicatorColor = Config::GetColor("hookIndicatorColor", Color(255, 210, 50, 255));
    g_indicatorPadX      = Config::GetFloat("indicatorPadX",      0.f);
    g_indicatorPadY      = Config::GetFloat("indicatorPadY",      -22.f);
    g_autoBreather     = Config::GetBool("autoBreather",    true);
    g_breatherHpPct    = Config::GetFloat("breatherHpPct",  90.f);
    g_autoShoot        = Config::GetBool("autoShoot",       true);
    g_shootKey.Load("shootKey");
    g_shootFovDeg      = Config::GetFloat("shootFovDeg",    90.f);
    g_showShootFov     = Config::GetBool("showShootFov",    true);
    g_shootSmoothing   = Config::GetFloat("shootSmoothing", 30.f);
    g_hitboxScale      = Config::GetFloat("hitboxScale",    1.0f);
    g_rmbMinDist       = Config::GetFloat("rmbMinDist",     15.f);
    g_rmbMaxDist       = Config::GetFloat("rmbMaxDist",     30.f);
    g_comboEnabled       = Config::GetBool("comboEnabled",        false);
    g_pullTriggerDist  = Config::GetFloat("pullTriggerDist", 3.75f);
    g_pullVelThresh    = Config::GetFloat("pullVelThresh",   0.08f);
    g_comboTimeout     = Config::GetFloat("comboTimeout",    2.f);
    g_shootDelay       = Config::GetFloat("shootDelay",      0.13f);
    g_meleeDelay       = Config::GetFloat("meleeDelay",      0.1f);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",          g_enabled);
    Config::SetBool("showDebugHud",     g_showDebugHud);
    Config::SetBool("hookAimbot",       g_hookAimbot);
    Config::SetFloat("fovDeg",          g_fovDeg);
    Config::SetBool("showFov",          g_showFov);
    Config::SetColor("fovColor",        g_fovColor);
    Config::SetFloat("smoothing",       g_smoothing);
    Config::SetBool("visOnly",          g_visOnly);
    Config::SetFloat("maxDist",         g_maxDist);
    Config::SetFloat("flickTime",       g_flickTime);
    Config::SetFloat("aimBone",         (float)g_aimBone);
    Config::SetBool("showHookIndicator",   g_showHookIndicator);
    Config::SetColor("hookIndicatorColor", g_hookIndicatorColor);
    Config::SetFloat("indicatorPadX",      g_indicatorPadX);
    Config::SetFloat("indicatorPadY",      g_indicatorPadY);
    Config::SetBool("autoBreather",    g_autoBreather);
    Config::SetFloat("breatherHpPct",  g_breatherHpPct);
    Config::SetBool("autoShoot",       g_autoShoot);
    g_shootKey.Save("shootKey");
    Config::SetFloat("shootFovDeg",    g_shootFovDeg);
    Config::SetBool("showShootFov",    g_showShootFov);
    Config::SetFloat("shootSmoothing", g_shootSmoothing);
    Config::SetFloat("hitboxScale",    g_hitboxScale);
    Config::SetFloat("rmbMinDist",     g_rmbMinDist);
    Config::SetFloat("rmbMaxDist",     g_rmbMaxDist);
    Config::SetBool("comboEnabled",       g_comboEnabled);
    Config::SetFloat("pullTriggerDist", g_pullTriggerDist);
    Config::SetFloat("pullVelThresh",   g_pullVelThresh);
    Config::SetFloat("comboTimeout",    g_comboTimeout);
    Config::SetFloat("shootDelay",      g_shootDelay);
    Config::SetFloat("meleeDelay",      g_meleeDelay);
    Config::Save();
}

extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled",   &g_enabled);
    ImGui::Checkbox("Debug HUD", &g_showDebugHud);
    ImGui::Separator();

    ImGui::Checkbox("Hook Aimbot", &g_hookAimbot);
    if (g_hookAimbot)
    {
        ImGui::SliderFloat("FOV (deg)",      &g_fovDeg,    0.f,  360.f);
        ImGui::Checkbox("Show FOV",          &g_showFov);
        if (g_showFov)
            ImGui::ColorPicker("FOV Color",  &g_fovColor);
        ImGui::SliderFloat("Smoothing",      &g_smoothing, 0.f,  1500.f);
        ImGui::Checkbox("Visible Only",      &g_visOnly);
        ImGui::SliderFloat("Max Dist (m)",   &g_maxDist,   0.f,  60.f);
        ImGui::SliderFloat("Flick Time (s)", &g_flickTime, 0.1f, 0.8f);
        ImGui::Combo("Aim Bone",             &g_aimBone,   kAimBones);
    }
    ImGui::Separator();

    ImGui::Checkbox("Hook Indicator", &g_showHookIndicator);
    if (g_showHookIndicator)
    {
        ImGui::ColorPicker("Indicator Color", &g_hookIndicatorColor);
        ImGui::SliderFloat("Indicator Pad X", &g_indicatorPadX, -200.f, 200.f);
        ImGui::SliderFloat("Indicator Pad Y", &g_indicatorPadY, -200.f, 200.f);
    }
    ImGui::Separator();

    ImGui::Checkbox("Auto Shoot (hold key)", &g_autoShoot);
    if (g_autoShoot)
    {
        g_shootKey.Render("Shoot Key");
        ImGui::SliderFloat("Shoot FOV (deg)", &g_shootFovDeg,     0.f,  360.f);
        ImGui::Checkbox("Show Shoot FOV",     &g_showShootFov);
        ImGui::SliderFloat("Shoot Smoothing", &g_shootSmoothing,  0.f,  1500.f);
        ImGui::SliderFloat("Hitbox Scale",    &g_hitboxScale,     0.5f, 2.f);
        ImGui::SliderFloat("RMB Min Dist (m)", &g_rmbMinDist,    0.f,  50.f);
        ImGui::SliderFloat("RMB Max Dist (m)", &g_rmbMaxDist,    0.f,  50.f);
    }
    ImGui::Separator();

    ImGui::Checkbox("Auto Take a Breather", &g_autoBreather);
    if (g_autoBreather)
        ImGui::SliderFloat("Breather HP %", &g_breatherHpPct, 10.f, 99.f);
    ImGui::Separator();

    ImGui::Checkbox("Combo", &g_comboEnabled);
    if (g_comboEnabled)
    {
        ImGui::SliderFloat("Pull Trigger Dist (m)", &g_pullTriggerDist, 1.f,  10.f);
        ImGui::SliderFloat("Pull Vel Threshold",    &g_pullVelThresh,   0.01f, 0.5f);
        ImGui::SliderFloat("Combo Timeout (s)",     &g_comboTimeout,    0.5f,  4.f);
        ImGui::SliderFloat("Shoot Delay (s)",       &g_shootDelay,      0.f,   0.5f);
        ImGui::SliderFloat("Melee Delay (s)",       &g_meleeDelay,      0.f,   0.5f);
    }
}

// ─────────────────────────────────────────────
//  on_frame — entity snapshot
// ─────────────────────────────────────────────

extern "C" void on_frame(float)
{
    int count = GetPlayerCount();
    g_entityCount = count < 32 ? count : 32;
    for (int i = 0; i < g_entityCount; ++i)
    {
        xn_get_entity(i, &g_entities[i]);
        if (g_entities[i].isLocalPlayer)
        {
            g_localPos    = g_entities[i].position;
            g_localTeam   = (int32_t)g_entities[i].team;
            g_localHp     = g_entities[i].health;
            g_localMaxHp  = g_entities[i].maxHealth;
        }
    }

    if (g_targetIdx >= 0 && g_targetIdx < g_entityCount)
        g_targetDist = xn_get_distance_to_entity(g_targetIdx);

    if (!g_enabled || !IsIngame()) return;

    g_shootKey.Update();

    float now = GetTime();

    // LMouse / RMouse — combo always uses LMouse; auto shoot picks by distance
    bool comboFiring = (now < g_shootHoldEnd);
    bool autoHit     = g_autoShoot && g_shootTargetValid && g_shootHitbox >= 0;
    bool useRmb      = !comboFiring && autoHit
                       && g_shootTargetDist >= g_rmbMinDist
                       && g_shootTargetDist <= g_rmbMaxDist;
    bool useLmb      = comboFiring || (autoHit && !useRmb);

    if (useLmb) PressGameButton(GameButton::LMouse); else ReleaseGameButton(GameButton::LMouse);
    if (useRmb) PressGameButton(GameButton::RMouse); else ReleaseGameButton(GameButton::RMouse);

    // Melee — combo hold only
    if (now < g_meleeHoldEnd)
        PressGameButton(GameButton::Melee);
    else
        ReleaseGameButton(GameButton::Melee);

    // Auto Take a Breather — press E when HP% < threshold and skill is ready
    if (g_autoBreather && g_localMaxHp > 0.f)
    {
        float hpPct      = g_localHp / g_localMaxHp * 100.f;
        SkillCooldown s2 = GetSkill2Cooldown();
        bool needHeal    = hpPct > 0.f && hpPct < g_breatherHpPct;
        bool skillReady  = !s2.IsOnCooldown() && !IsSkill2Active();
        if (needHeal && skillReady)
            PressGameButton(GameButton::Skill2);
        else
            ReleaseGameButton(GameButton::Skill2);
    }
}

// ─────────────────────────────────────────────
//  on_render — hook aimbot state machine + draw
// ─────────────────────────────────────────────

extern "C" void on_render()
{
    if (!g_enabled) return;

    float now        = GetTime();
    bool  hookActive = IsSkill1Active();

    // Re-resolve target by stable entity ID each frame
    if (g_haPhase != HA_IDLE && g_targetId != 0)
    {
        g_targetIdx = -1;
        for (int i = 0; i < g_entityCount; ++i)
            if (g_entities[i].id == g_targetId) { g_targetIdx = i; break; }
    }

    if (g_hookAimbot)
    {
        // Rising edge — hook thrown, lock target and start flick
        if (hookActive && !g_hookPrev && g_haPhase == HA_IDLE)
        {
            float   fovPx = (g_fovDeg / 360.f) * ScreenSize().x;
            int     flags = TargetFlags::Enemy;
            if (g_visOnly) flags |= TargetFlags::Visible;
            int32_t raw   = xn_find_best_target_fov(fovPx, Bone::Head, flags);
            int32_t tIdx  = (raw >= 0 && raw != (int32_t)0xFFFFFFFF) ? raw : -1;

            if (tIdx >= 0 && tIdx < g_entityCount && g_entities[tIdx].alive)
            {
                float dx = g_entities[tIdx].position.x - g_localPos.x;
                float dy = g_entities[tIdx].position.y - g_localPos.y;
                float dz = g_entities[tIdx].position.z - g_localPos.z;

                if (g_maxDist <= 0.f || dx*dx + dy*dy + dz*dz <= g_maxDist * g_maxDist)
                {
                    g_targetId  = g_entities[tIdx].id;
                    g_targetIdx = tIdx;
                    g_targetPos = g_entities[tIdx].position;
                    AimResetSmoothing();
                    g_haPhase = HA_FLICK;
                    g_phaseAt = now;
                }
            }
        }

        // HA_FLICK — aim at target for g_flickTime then stop or enter combo wait
        if (g_haPhase == HA_FLICK)
        {
            AimAtTarget();
            if (now - g_phaseAt >= g_flickTime)
            {
                if (g_comboEnabled)
                {
                    g_pullDetected = false;
                    g_prevDist     = g_targetDist;
                    g_haPhase      = HA_COMBO_WAIT;
                    g_phaseAt      = now;
                }
                else
                {
                    g_haPhase  = HA_IDLE;
                    g_targetId = 0;
                }
            }
        }

        // HA_COMBO_WAIT — watch target velocity/proximity to detect successful pull
        if (g_haPhase == HA_COMBO_WAIT)
        {
            // Velocity check: target approaching faster than threshold → being pulled
            float approach = g_prevDist - g_targetDist;
            if (approach > g_pullVelThresh)
                g_pullDetected = true;
            g_prevDist = g_targetDist;

            // Fire when pull confirmed and target is close enough
            if (g_pullDetected && g_targetDist <= g_pullTriggerDist)
            {
                AimResetSmoothing();
                g_haPhase = HA_SHOOT;
                g_phaseAt = now;
            }
            // Timeout — hook missed or target died
            else if (now - g_phaseAt >= g_comboTimeout)
            {
                g_haPhase  = HA_IDLE;
                g_targetId = 0;
            }
        }

        // HA_SHOOT — hold aim every frame, arm shoot hold after delay
        if (g_haPhase == HA_SHOOT)
        {
            AimAtTarget();
            if (now - g_phaseAt >= g_shootDelay)
            {
                g_shootHoldEnd = now + kComboHoldDur;
                g_haPhase      = HA_MELEE;
                g_phaseAt      = now;
            }
        }
        // HA_MELEE — arm melee hold after delay (else if so both don't run same frame)
        else if (g_haPhase == HA_MELEE)
        {
            if (now - g_phaseAt >= g_meleeDelay)
            {
                g_meleeHoldEnd = now + kComboHoldDur;
                g_haPhase      = HA_IDLE;
                g_targetId     = 0;
            }
        }

    }
    else if (g_haPhase != HA_IDLE)
    {
        g_haPhase  = HA_IDLE;
        g_targetId = 0;
    }

    g_hookPrev = hookActive;

    // Hook range indicator — label above any hookable enemy
    if (g_showHookIndicator && g_haPhase == HA_IDLE)
    {
        float maxDistSq = g_maxDist * g_maxDist;
        for (int i = 0; i < g_entityCount; ++i)
        {
            const PluginEntity& e = g_entities[i];
            if (!e.alive || e.isLocalPlayer) continue;
            if ((int32_t)e.team == g_localTeam) continue;

            float dx = e.position.x - g_localPos.x;
            float dy = e.position.y - g_localPos.y;
            float dz = e.position.z - g_localPos.z;
            if (dx*dx + dy*dy + dz*dz > maxDistSq) continue;

            Vector3 headPos;
            xn_get_bone_pos(i, Bone::Head, &headPos);
            Vector2 sp;
            if (!WorldToScreen(headPos, sp)) continue;

            Draw::TextCentered(sp.x + g_indicatorPadX, sp.y + g_indicatorPadY, g_hookIndicatorColor, "HOOKABLE");
        }
    }

    // Auto shoot — target selection and aim (runs every frame key is held)
    if (g_autoShoot)
    {
        bool held = g_shootKey.IsDown();
        if (!held)
        {
            g_shootTargetIdx   = -1;
            g_shootTargetValid = false;
            g_shootHitbox      = -1;
        }
        else
        {
            float fovPx  = (g_shootFovDeg / 360.f) * ScreenSize().x;
            int   flags  = TargetFlags::Enemy | TargetFlags::Visible;

            // Check if locked target still alive
            bool lockedAlive = false;
            if (g_shootTargetIdx >= 0 && g_shootTargetIdx < g_entityCount)
                lockedAlive = g_entities[g_shootTargetIdx].alive;

            if (!lockedAlive)
            {
                int32_t raw  = xn_find_best_target_fov(fovPx, Bone::Head, flags);
                int32_t tIdx = (raw >= 0 && raw != (int32_t)0xFFFFFFFF) ? raw : -1;
                if (tIdx != g_shootTargetIdx) AimResetSmoothing();
                g_shootTargetIdx = tIdx;
            }

            if (g_shootTargetIdx >= 0)
            {
                g_shootTargetValid = true;
                g_shootTargetDist  = xn_get_distance_to_entity(g_shootTargetIdx);
                AimAtBone(g_shootTargetIdx, Bone::Head, g_shootSmoothing);
                g_shootHitbox = AimHitsHitbox(g_shootTargetIdx, g_hitboxScale);
            }
            else
            {
                g_shootTargetValid = false;
                g_shootHitbox      = -1;
                g_shootTargetDist  = 0.f;
            }
        }
    }

    // FOV circles
    if (g_hookAimbot && g_showFov && g_fovDeg > 0.f && g_fovDeg < 360.f)
    {
        float fovPx = (g_fovDeg / 360.f) * ScreenSize().x;
        Draw::Circle(ScreenCenter(), fovPx, g_fovColor, 1.f);
    }
    if (g_autoShoot && g_showShootFov && g_shootFovDeg > 0.f && g_shootFovDeg < 360.f)
    {
        float fovPx = (g_shootFovDeg / 360.f) * ScreenSize().x;
        Draw::Circle(ScreenCenter(), fovPx, g_shootFovColor, 1.f);
    }

    DebugHud();
}
