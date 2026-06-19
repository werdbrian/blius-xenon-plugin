// Genji V1 Plugin (hero-specific — only loads on Genji)
// Two features:
//   1. Dash Combo  — one key press runs the full sequence: snap aim -> Swift Strike
//      (Skill1) -> wait for dash to finish -> re-aim selected bone -> RMouse -> Melee.
//      State machine: CP_IDLE -> CP_DETECT -> CP_AIM -> CP_MELEE.
//   2. Dash Assist — when YOU manually press Swift Strike, it locks the target you were
//      looking at, then after the dash optionally auto-RClicks / auto-melees.
// Close Aim Adjust nudges aim upward at point-blank range. Tunable FOV, bone, delays,
// max distance, and smoothing in the menu.
#include <xenon/SDK.hpp>
using namespace xenon;

static void PulseGameButton(uint32_t btn) { PressGameButton(btn); ReleaseGameButton(btn); }

XENON_PLUGIN_INFO(
    "genjiv1", "Genji V1", "c", "", "1.0", HeroId::Genji,
    PluginFlags::HasOverlay | PluginFlags::HasMenu | PluginFlags::HeroSpecific
)

// â”€â”€ Settings â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static bool   g_enabled         = true;
static bool   g_showDebugHud    = true;

// Dash combo (one key press â†’ dash â†’ aim â†’ shoot â†’ melee)
static bool   g_comboEnabled    = true;
static Hotkey g_comboKey(VK::XButton2);
static float  g_comboFovDeg     = 90.f;
static bool   g_showComboFov    = true;
static Color  g_comboFovColor   = Color(255, 180, 50, 80);
static float  g_comboMaxDist    = 14.f;
static float  g_comboSmoothing  = 0.f;

static float  g_shootDelay          = 0.05f; // seconds between turn and RMouse
static float  g_meleeDelay          = 0.05f; // seconds between RMouse and Melee
static float  g_meleeMaxDist        = 3.f;   // skip melee if target is farther than this (m); 0 = always

// Combo aim bone
enum { AB_HEAD = 0, AB_NECK, AB_CHEST, AB_CLOSEST };
static const char* kAimBones = "Head\0Neck\0Chest\0Closest\0";
static int g_comboAimBone = AB_HEAD;

// Close aim adjust â€” adds upward Y offset to the aim bone when target is very close
static bool  g_closeAimEnabled = true;
static float g_closeAimDist    = 1.5f;  // meters; offset applies when closer than this
static float g_closeAimOffsetY = 0.3f;  // world units added to bone Y at point-blank (positive = up)
static bool  g_closeAimDynamic = true;  // scale offset by distance (closer = more offset)
static float g_closeAimCurve   = 1.0f;  // falloff curve exponent (1=linear, >1=spikes near zero)

// â”€â”€ Runtime state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static PluginEntity g_entities[32]{};
static int          g_entityCount = 0;
static Vector3      g_localPos{};
static int32_t      g_localTeam   = -1;
static float        g_targetDist  = 0.f;

// â”€â”€ Combo state machine â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//
//  CP_IDLE   â†’ rising edge of combo key + valid target in range + strike ready
//            â†’ snap aim, fire Skill1 (Swift Strike) â†’ CP_DETECT
//
//  CP_DETECT â†’ latch g_skill1WasActive once IsSkill1Active() goes true
//            â†’ if skill never starts within kDashStartTimeout â†’ abort
//            â†’ trigger when skill goes activeâ†’inactive (dash finished)
//            â†’ snap aim to selected bone â†’ CP_AIM
//
//  CP_AIM    â†’ wait g_shootDelay â†’ fire RMouse â†’ CP_MELEE
//
//  CP_MELEE  â†’ wait g_meleeDelay â†’ fire Melee â†’ CP_IDLE
//
enum ComboPhase { CP_IDLE = 0, CP_DETECT, CP_AIM, CP_MELEE };

static ComboPhase g_comboPhase       = CP_IDLE;
static float      g_comboPhaseAt     = 0.f;
static uint32_t   g_comboTargetId    = 0;   // stable entity UID â€” array index shifts after dash
static int32_t    g_comboTargetIdx   = -1;  // re-resolved each frame from g_comboTargetId
static Vector3    g_dashTargetPos    = {};  // fallback aim position if entity unresolvable post-dash
static bool       g_skill1WasActive  = false; // supplementary: saw active state
static bool       g_skill1CdStarted  = false; // primary latch: cooldown appeared = dash fired

static constexpr float kDashStartTimeout = 0.5f;

// â”€â”€ Dash assist â€” fires when player manually uses Swift Strike â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static bool  g_dashAssistEnabled   = false;
static float g_dashAssistFovDeg    = 360.f;
static bool  g_dashAssistAutoShoot = false;
static bool  g_dashAssistAutoMelee = false;
static bool  g_showDaFov           = true;
static Color g_daFovColor          = Color(100, 200, 255, 80);

enum DashAssistPhase { DA_IDLE = 0, DA_DETECT, DA_SHOOT, DA_MELEE };
static DashAssistPhase g_daPhase       = DA_IDLE;
static float           g_daPhaseAt     = 0.f;
static uint32_t        g_daTargetId    = 0;
static int32_t         g_daTargetIdx   = -1;
static Vector3         g_daTargetPos   = {};
static bool            g_daPrevCdActive  = false;
static bool            g_daSkill1Prev    = false;

// powf isn't available in the WASM runtime â€” approximation via repeated multiply
// + linear blend for the fractional part. Accurate enough for curve values in [0.1, 4].
static float CurvePow(float b, float e)
{
    if (b <= 0.f) return 0.f;
    int   steps  = (int)e;
    float result = 1.f;
    for (int i = 0; i < steps; ++i) result *= b;
    float frac = e - (float)steps;
    if (frac > 0.f) result *= 1.f - frac + frac * b;
    return result;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Shared aim helpers (used by combo and dash assist)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static int ResolveAimBoneId(int32_t idx)
{
    if (g_comboAimBone == AB_NECK)  return Bone::Neck;
    if (g_comboAimBone == AB_CHEST) return Bone::Chest;
    if (g_comboAimBone == AB_CLOSEST)
    {
        static const int kCandidates[] = { Bone::Head, Bone::Neck, Bone::Chest };
        Vector2 ctr = ScreenCenter();
        int   best   = Bone::Head;
        float bestSq = 1e30f;
        for (int i = 0; i < 3; i++)
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

// Aims at entity idx with close-aim adjustment; falls back to fallbackPos if idx invalid.
static void AimAtEntityBone(int32_t idx, const Vector3& fallbackPos)
{
    if (idx < 0 || idx >= g_entityCount)
    {
        AimAtPosition(fallbackPos, g_comboSmoothing);
        return;
    }

    int boneId = ResolveAimBoneId(idx);

    // Close aim adjust: shift bone up when target is very close
    if (g_closeAimEnabled)
    {
        const PluginEntity& t = g_entities[idx];
        float dx = t.position.x - g_localPos.x;
        float dy = t.position.y - g_localPos.y;
        float dz = t.position.z - g_localPos.z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);

        if (dist <= g_closeAimDist && dist > 0.01f)
        {
            float offset;
            if (g_closeAimDynamic && g_closeAimDist > 0.f)
            {
                float tc = 1.f - dist / g_closeAimDist;
                if (tc < 0.f) tc = 0.f;
                offset = (g_closeAimOffsetY / dist) * CurvePow(tc, g_closeAimCurve);
            }
            else
            {
                offset = g_closeAimOffsetY;
            }

            Vector3 bonePos;
            xn_get_bone_pos(idx, boneId, &bonePos);
            bonePos.y += offset;
            AimAtPosition(bonePos, g_comboSmoothing);
            return;
        }
    }

    AimAtBone(idx, boneId, g_comboSmoothing);
}

static void ComboAimAtTarget() { AimAtEntityBone(g_comboTargetIdx, g_dashTargetPos); }

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Debug HUD
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static void DebugHud()
{
    if (!g_showDebugHud) return;

    float x = 10.f, y = 10.f;
    TextBuilder<64> tb;

    Draw::TextShadow(x, y, Color::White(), "[ Genji V1 ]"); y += 18.f;

    // â”€â”€ Skill states (local player) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    SkillCooldown s1 = GetSkill1Cooldown();
    SkillCooldown s2 = GetSkill2Cooldown();
    bool s1Active  = IsSkill1Active();
    bool s2Active  = IsSkill2Active();
    bool ultActive = IsUltActive();
    bool ultReady  = IsUltReady();

    // Swift Strike
    if (s1Active) {
        Draw::TextShadow(x, y, Color::Cyan(), "Strike:  ACTIVE"); y += 16.f;
    } else if (s1.IsOnCooldown()) {
        tb.put("Strike:  ").putFloat(s1.current, 1).put("s");
        Draw::TextShadow(x, y, Color(160, 160, 160, 200), tb.c_str()); y += 16.f; tb.clear();
    } else {
        Draw::TextShadow(x, y, Color::Green(), "Strike:  READY"); y += 16.f;
    }

    // Deflect
    if (s2Active) {
        Draw::TextShadow(x, y, Color::Cyan(), "Deflect: ACTIVE"); y += 16.f;
    } else if (s2.IsOnCooldown()) {
        tb.put("Deflect: ").putFloat(s2.current, 1).put("s");
        Draw::TextShadow(x, y, Color(160, 160, 160, 200), tb.c_str()); y += 16.f; tb.clear();
    } else {
        Draw::TextShadow(x, y, Color::Green(), "Deflect: READY"); y += 16.f;
    }

    // Dragonblade
    if (ultActive) {
        Draw::TextShadow(x, y, Color::Orange(), "Blade:   ACTIVE"); y += 16.f;
    } else if (ultReady) {
        Draw::TextShadow(x, y, Color::Orange(), "Blade:   READY"); y += 16.f;
    } else {
        tb.put("Blade:   ").putFloat(GetUltCharge(), 0).put("%");
        Draw::TextShadow(x, y, Color(200, 200, 200, 200), tb.c_str()); y += 16.f; tb.clear();
    }

    y += 6.f;

    // â”€â”€ Combo state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    static const char* kPhaseNames[] = { "idle", "DETECT", "AIM", "MELEE" };
    if (g_comboPhase != CP_IDLE) {
        tb.put("Combo:   ").put(kPhaseNames[(int)g_comboPhase]);
        Draw::TextShadow(x, y, Color::Orange(), tb.c_str()); y += 16.f; tb.clear();
    } else {
        Draw::TextShadow(x, y, Color(120, 120, 120, 200), "Combo:   idle"); y += 16.f;
    }

    // â”€â”€ Dash assist state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    static const char* kDaNames[] = { "idle", "DETECT", "SHOOT", "MELEE" };
    if (g_daPhase != DA_IDLE) {
        tb.put("DashAst: ").put(kDaNames[(int)g_daPhase]);
        Draw::TextShadow(x, y, Color::Cyan(), tb.c_str()); y += 16.f; tb.clear();
    } else {
        Draw::TextShadow(x, y, Color(120, 120, 120, 200), "DashAst: idle"); y += 16.f;
    }

    y += 6.f;

    // â”€â”€ Active target â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    Draw::TextShadow(x, y, Color::White(), "[ Target ]"); y += 18.f;

    int32_t showIdx = g_comboTargetIdx;
    if (showIdx < 0 || showIdx >= g_entityCount) {
        Draw::TextShadow(x, y, Color(160, 160, 160, 200), "No target");
        return;
    }

    const PluginEntity& t = g_entities[showIdx];
    float hpPct = t.maxHealth > 0.f ? t.health / t.maxHealth : 0.f;

    tb.put("Hero    ").put(GetHeroName(t.heroId));
    Draw::TextShadow(x, y, Color::White(), tb.c_str()); y += 16.f; tb.clear();

    tb.put("HP      ").putInt((int)t.health).put(" / ").putInt((int)t.maxHealth);
    Draw::TextShadow(x, y, Color::HealthGradient(hpPct), tb.c_str()); y += 16.f; tb.clear();

    tb.put("Dist    ").putFloat(g_targetDist, 1).put(" m");
    bool inRange = g_comboMaxDist <= 0.f || g_targetDist <= g_comboMaxDist;
    Draw::TextShadow(x, y, inRange ? Color::White() : Color::Red(), tb.c_str());
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Lifecycle
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

extern "C" void on_load()
{
    g_enabled           = Config::GetBool("enabled",          true);
    g_showDebugHud      = Config::GetBool("showDebugHud",     true);
    g_comboEnabled      = Config::GetBool("comboEnabled",     true);
    g_comboKey.Load("comboKey");
    g_comboFovDeg       = Config::GetFloat("comboFov",        90.f);
    g_showComboFov      = Config::GetBool("showComboFov",     true);
    g_comboFovColor     = Config::GetColor("comboFovColor",   Color(255, 180, 50, 80));
    g_comboMaxDist      = Config::GetFloat("comboMaxDist",    14.f);
    g_comboSmoothing    = Config::GetFloat("comboSmooth",     0.f);
    g_shootDelay        = Config::GetFloat("shootDelay",       0.10f);
    g_meleeDelay        = Config::GetFloat("meleeDelay",       0.10f);
    g_meleeMaxDist      = Config::GetFloat("meleeMaxDist",     3.f);
    g_comboAimBone      = (int)Config::GetFloat("comboAimBone", 0.f);
    g_closeAimEnabled   = Config::GetBool("closeAimEnabled",   true);
    g_closeAimDist      = Config::GetFloat("closeAimDist",     1.5f);
    g_closeAimOffsetY   = Config::GetFloat("closeAimOffsetY",  0.3f);
    g_closeAimDynamic   = Config::GetBool("closeAimDynamic",   true);
    g_closeAimCurve       = Config::GetFloat("closeAimCurve",      1.0f);
    g_dashAssistEnabled   = Config::GetBool("dashAssistEnabled",    false);
    g_dashAssistFovDeg    = Config::GetFloat("dashAssistFov",       360.f);
    g_dashAssistAutoShoot = Config::GetBool("dashAssistAutoShoot",  false);
    g_dashAssistAutoMelee = Config::GetBool("dashAssistAutoMelee",  false);
    g_showDaFov           = Config::GetBool("showDaFov",            true);
    g_daFovColor          = Config::GetColor("daFovColor",          Color(100, 200, 255, 80));
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",          g_enabled);
    Config::SetBool("showDebugHud",     g_showDebugHud);
    Config::SetBool("comboEnabled",     g_comboEnabled);
    g_comboKey.Save("comboKey");
    Config::SetFloat("comboFov",        g_comboFovDeg);
    Config::SetBool("showComboFov",     g_showComboFov);
    Config::SetColor("comboFovColor",   g_comboFovColor);
    Config::SetFloat("comboMaxDist",    g_comboMaxDist);
    Config::SetFloat("comboSmooth",     g_comboSmoothing);
    Config::SetFloat("shootDelay",      g_shootDelay);
    Config::SetFloat("meleeDelay",      g_meleeDelay);
    Config::SetFloat("meleeMaxDist",    g_meleeMaxDist);
    Config::SetFloat("comboAimBone",    (float)g_comboAimBone);
    Config::SetBool("closeAimEnabled",  g_closeAimEnabled);
    Config::SetFloat("closeAimDist",    g_closeAimDist);
    Config::SetFloat("closeAimOffsetY", g_closeAimOffsetY);
    Config::SetBool("closeAimDynamic",  g_closeAimDynamic);
    Config::SetFloat("closeAimCurve",     g_closeAimCurve);
    Config::SetBool("dashAssistEnabled",   g_dashAssistEnabled);
    Config::SetFloat("dashAssistFov",      g_dashAssistFovDeg);
    Config::SetBool("dashAssistAutoShoot", g_dashAssistAutoShoot);
    Config::SetBool("dashAssistAutoMelee", g_dashAssistAutoMelee);
    Config::SetBool("showDaFov",           g_showDaFov);
    Config::SetColor("daFovColor",         g_daFovColor);
    Config::Save();
}

extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled",   &g_enabled);
    ImGui::Checkbox("Debug HUD", &g_showDebugHud);
    ImGui::Separator();

    ImGui::Checkbox("Dash Combo", &g_comboEnabled);
    if (g_comboEnabled)
    {
        g_comboKey.Render("Combo Key");
        ImGui::SliderFloat("Combo FOV (deg)",  &g_comboFovDeg,      0.f,   360.f);
        ImGui::Checkbox("Show Combo FOV",      &g_showComboFov);
        if (g_showComboFov)
            ImGui::ColorPicker("Combo FOV Color", &g_comboFovColor);
        ImGui::SliderFloat("Max Dist (m)",     &g_comboMaxDist,     0.f,   20.f);
        ImGui::SliderFloat("Aim Smoothing",    &g_comboSmoothing,   0.f,   1500.f);
        ImGui::Combo("Aim Bone",               &g_comboAimBone,     kAimBones);
        ImGui::Separator();
        ImGui::SliderFloat("Shoot Delay (s)",  &g_shootDelay,    0.f, 0.5f);
        ImGui::SliderFloat("Melee Delay (s)",  &g_meleeDelay,    0.f, 0.5f);
        ImGui::SliderFloat("Melee Max Dist (m)", &g_meleeMaxDist, 0.f, 8.f);
        ImGui::Separator();
        ImGui::Checkbox("Close Aim Adjust",    &g_closeAimEnabled);
        if (g_closeAimEnabled)
        {
            ImGui::SliderFloat("Close Dist (m)",   &g_closeAimDist,    0.f,  5.f);
            ImGui::SliderFloat("Close Offset Y",   &g_closeAimOffsetY, -1.f, 1.f);
            ImGui::Checkbox("Dynamic Offset",      &g_closeAimDynamic);
            if (g_closeAimDynamic)
                ImGui::SliderFloat("Offset Curve", &g_closeAimCurve,   0.1f, 4.f);
        }
    }
    ImGui::Separator();

    ImGui::Checkbox("Dash Assist", &g_dashAssistEnabled);
    if (g_dashAssistEnabled)
    {
        ImGui::SliderFloat("Assist FOV (deg)", &g_dashAssistFovDeg, 30.f, 360.f);
        ImGui::Checkbox("Show Assist FOV",     &g_showDaFov);
        if (g_showDaFov)
            ImGui::ColorPicker("Assist FOV Color", &g_daFovColor);
        ImGui::Checkbox("Auto RClick",         &g_dashAssistAutoShoot);
        ImGui::Checkbox("Auto Melee",          &g_dashAssistAutoMelee);
        ImGui::Text("Reuses Aim Bone, delays and Melee Max Dist from Dash Combo.");
    }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  on_frame â€” entity snapshot
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

extern "C" void on_frame(float)
{
    g_comboKey.Update();

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

    if (g_comboTargetIdx >= 0 && g_comboTargetIdx < g_entityCount)
        g_targetDist = xn_get_distance_to_entity(g_comboTargetIdx);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  on_render â€” combo state machine + draw
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

extern "C" void on_render()
{
    if (!g_enabled) return;

    float now = GetTime();

    // â”€â”€ Re-resolve combo target index from stable entity ID â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (g_comboPhase != CP_IDLE && g_comboTargetId != 0)
    {
        g_comboTargetIdx = -1;
        for (int i = 0; i < g_entityCount; i++)
            if (g_entities[i].id == g_comboTargetId) { g_comboTargetIdx = i; break; }
    }

    // â”€â”€ Combo: rising edge of key triggers the sequence â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    static bool s_wasComboDown = false;
    bool comboDown   = g_comboEnabled && g_comboKey.IsDown();
    bool comboRising = comboDown && !s_wasComboDown;
    s_wasComboDown   = comboDown;

    if (comboRising && g_comboPhase == CP_IDLE)
    {
        SkillCooldown s1 = GetSkill1Cooldown();
        WeaponInfo    wi{};
        GetWeaponInfo(InputFlag::SecondaryFire, wi);

        if (!s1.IsOnCooldown() && wi.shootable && !wi.reloading)
        {
            float combFovPx = (g_comboFovDeg / 360.f) * ScreenSize().x;
            int32_t rawIdx  = xn_find_best_target_fov(combFovPx, Bone::Head,
                                  TargetFlags::Enemy | TargetFlags::Visible);
            int32_t tIdx    = (rawIdx >= 0 && rawIdx != (int32_t)0xFFFFFFFF) ? rawIdx : -1;

            if (tIdx >= 0 && tIdx < g_entityCount && g_entities[tIdx].alive)
            {
                float dx = g_entities[tIdx].position.x - g_localPos.x;
                float dy = g_entities[tIdx].position.y - g_localPos.y;
                float dz = g_entities[tIdx].position.z - g_localPos.z;
                float distSq = dx*dx + dy*dy + dz*dz;

                if (g_comboMaxDist <= 0.f || distSq <= g_comboMaxDist * g_comboMaxDist)
                {
                    g_comboTargetId   = g_entities[tIdx].id;
                    g_comboTargetIdx  = tIdx;
                    g_dashTargetPos   = g_entities[tIdx].position;
                    g_skill1WasActive = false;
                    g_skill1CdStarted = false;

                    AimResetSmoothing();
                    AimAtBone(tIdx, Bone::Head, 0.f);
                    PulseGameButton(GameButton::Skill1);

                    g_comboPhase   = CP_DETECT;
                    g_comboPhaseAt = now;
                }
            }
        }
    }

    // â”€â”€ CP_DETECT: watch for Swift Strike to finish, then snap aim â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (g_comboPhase == CP_DETECT)
    {
        bool          skill1Active = IsSkill1Active();
        SkillCooldown s1cd         = GetSkill1Cooldown();

        // Latch via active state (animation caught)
        if (skill1Active) g_skill1WasActive = true;

        // Latch via cooldown (more reliable, but resets on kill â€” so not always reliable alone)
        if (s1cd.IsOnCooldown()) g_skill1CdStarted = true;

        // Dash detected if EITHER latch fires â€” covers kill-reset (no cooldown but animation
        // was seen) and missed-animation-window (cooldown seen but active state wasn't polled)
        bool dashDetected = g_skill1WasActive || g_skill1CdStarted;

        if (!dashDetected && now - g_comboPhaseAt >= kDashStartTimeout)
        {
            // Neither signal fired â€” dash didn't execute, abort
            g_comboPhase = CP_IDLE; g_comboTargetId = 0;
        }
        else if (dashDetected && !skill1Active)
        {
            // Dash fired and animation is done â€” snap aim and proceed
            AimResetSmoothing();
            ComboAimAtTarget();

            g_comboPhase   = CP_AIM;
            g_comboPhaseAt = now;
        }
    }

    // â”€â”€ CP_AIM: hold aim every frame, fire RMouse after shoot delay â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (g_comboPhase == CP_AIM)
    {
        ComboAimAtTarget();
        if (now - g_comboPhaseAt >= g_shootDelay)
        {
            PulseGameButton(GameButton::RMouse);
            g_comboPhase   = CP_MELEE;
            g_comboPhaseAt = now;
        }
    }

    // â”€â”€ CP_MELEE: wait melee delay, fire Melee only if target still in range â”€â”€
    if (g_comboPhase == CP_MELEE && now - g_comboPhaseAt >= g_meleeDelay)
    {
        if (g_meleeMaxDist <= 0.f || g_comboTargetIdx < 0)
        {
            PulseGameButton(GameButton::Melee);
        }
        else
        {
            const PluginEntity& t = g_entities[g_comboTargetIdx];
            float dx = t.position.x - g_localPos.x;
            float dy = t.position.y - g_localPos.y;
            float dz = t.position.z - g_localPos.z;
            if (dx*dx + dy*dy + dz*dz <= g_meleeMaxDist * g_meleeMaxDist)
                PulseGameButton(GameButton::Melee);
        }
        g_comboPhase = CP_IDLE; g_comboTargetId = 0;
    }

    // â”€â”€ Dash assist â€” manual Swift Strike tracking â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        bool          cdNow        = GetSkill1Cooldown().IsOnCooldown();
        bool          skill1Active = IsSkill1Active();

        // Re-resolve stable target ID each frame during assist
        if (g_daPhase != DA_IDLE && g_daTargetId != 0)
        {
            g_daTargetIdx = -1;
            for (int i = 0; i < g_entityCount; i++)
                if (g_entities[i].id == g_daTargetId) { g_daTargetIdx = i; break; }
        }

        if (g_dashAssistEnabled && g_comboPhase == CP_IDLE)
        {
            if (g_daPhase == DA_IDLE)
            {
                // Rising edge of EITHER active state OR cooldown = dash fired
                bool skill1Rising = skill1Active && !g_daSkill1Prev;
                bool cdRising     = cdNow && !g_daPrevCdActive;
                if (skill1Rising || cdRising)
                {
                    // Lock target NOW â€” who we're looking at when we press the key
                    float daFovPx = (g_dashAssistFovDeg / 360.f) * ScreenSize().x;
                    int32_t raw   = xn_find_best_target_fov(daFovPx, Bone::Head,
                                        TargetFlags::Enemy | TargetFlags::Visible);
                    int32_t tIdx  = (raw >= 0 && raw != (int32_t)0xFFFFFFFF) ? raw : -1;

                    if (tIdx >= 0)
                    {
                        g_daTargetId  = g_entities[tIdx].id;
                        g_daTargetIdx = tIdx;
                        g_daTargetPos = g_entities[tIdx].position;
                        g_daPhase     = DA_DETECT;
                        g_daPhaseAt   = now;
                    }
                    // No target in FOV â€” don't enter detect at all
                }
            }

            if (g_daPhase == DA_DETECT)
            {
                // Wait for the dash animation to finish, then snap aim at locked target
                if (!skill1Active)
                {
                    AimResetSmoothing();
                    g_daPhase   = DA_SHOOT;
                    g_daPhaseAt = now;
                }
            }
        }
        else if (g_daPhase != DA_IDLE)
        {
            // Combo fired or assist disabled â€” reset
            g_daPhase = DA_IDLE; g_daTargetId = 0;
        }

        g_daPrevCdActive = cdNow;
        g_daSkill1Prev   = skill1Active;

        if (g_daPhase == DA_SHOOT)
        {
            AimAtEntityBone(g_daTargetIdx, g_daTargetPos);
            if (now - g_daPhaseAt >= g_shootDelay)
            {
                if (g_dashAssistAutoShoot)
                    PulseGameButton(GameButton::RMouse);

                if (g_dashAssistAutoMelee)
                {
                    g_daPhase   = DA_MELEE;
                    g_daPhaseAt = now;
                }
                else
                {
                    g_daPhase = DA_IDLE; g_daTargetId = 0;
                }
            }
        }

        if (g_daPhase == DA_MELEE && now - g_daPhaseAt >= g_meleeDelay)
        {
            if (g_daTargetIdx >= 0 && g_meleeMaxDist > 0.f)
            {
                const PluginEntity& t = g_entities[g_daTargetIdx];
                float dx = t.position.x - g_localPos.x;
                float dy = t.position.y - g_localPos.y;
                float dz = t.position.z - g_localPos.z;
                if (dx*dx + dy*dy + dz*dz <= g_meleeMaxDist * g_meleeMaxDist)
                    PulseGameButton(GameButton::Melee);
            }
            else if (g_meleeMaxDist <= 0.f)
            {
                PulseGameButton(GameButton::Melee);
            }
            g_daPhase = DA_IDLE; g_daTargetId = 0;
        }

    }

    // â”€â”€ FOV circles â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (g_comboEnabled && g_showComboFov && g_comboFovDeg > 0.f && g_comboFovDeg < 360.f)
    {
        float combFovPx = (g_comboFovDeg / 360.f) * ScreenSize().x;
        Draw::Circle(ScreenCenter(), combFovPx, g_comboFovColor, 1.f);
    }

    if (g_dashAssistEnabled && g_showDaFov && g_dashAssistFovDeg > 0.f && g_dashAssistFovDeg < 360.f)
    {
        float daFovPx = (g_dashAssistFovDeg / 360.f) * ScreenSize().x;
        Draw::Circle(ScreenCenter(), daFovPx, g_daFovColor, 1.f);
    }

    DebugHud();
}

