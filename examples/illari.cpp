// Illari Plugin
// Hold trigger key: aim assist + auto primary fire when solar rifle charge >= threshold

#include <xenon/SDK.hpp>
using namespace xenon;

static float    g_stiffness       = 30.f;
static float    g_fovRadius       = 200.f;
static int      g_targetMode      = 0; // 0=closest crosshair, 1=lowest HP
static Hotkey   g_triggerKey(1);  // default LMB; click-to-bind in menu
static float    g_chargeThreshold = 1.0f;  // fire when charge >= this (0.0-1.0)
static float    g_killHp          = 25.f;  // also fire below full charge if target HP <= this
static int32_t  g_cachedTarget    = -1;
static bool     g_targetValid     = false;
static float    g_targetHp        = 0.f;
static int      g_cachedHitbox    = -1;
static int      g_bestBone        = Bone::Chest;
static bool     g_enabled         = true;
static bool     g_autoShoot       = true;
static bool     g_autoMelee       = true;
static float    g_meleeRange      = 2.f;
static float    g_meleeHold       = 0.f;
static float    g_meleeHoldSet    = 0.10f;
static float    g_hitboxScale     = 1.0f;
static uint64_t g_heroId          = 0;
static bool     g_heroLock        = false;

// Bone candidates — order matches kAimBones
static const int kAimBones[]     = {
    Bone::Head, Bone::Neck, Bone::Chest, Bone::Body, Bone::Pelvis,
    Bone::LShoulder, Bone::RShoulder
};
static const char* kBoneNames[]  = {
    "Head", "Neck", "Chest", "Body", "Pelvis", "L Shoulder", "R Shoulder"
};
static const int kAimBoneCount   = 7;
static bool g_boneEnabled[7]     = { true, true, true, true, true, false, false };

XENON_PLUGIN_INFO(
    "illari",
    "Illari",
    "Xenon",
    "Aim assist + auto solar rifle when charge is full.",
    "1.0",
    0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

extern "C" void on_load()
{
    g_stiffness       = Config::GetFloat("stiffness",       30.f);
    g_fovRadius       = Config::GetFloat("fovRadius",       200.f);
    g_targetMode      = Config::GetInt("targetMode",        0);
    g_triggerKey.Load("triggerKey");
    g_chargeThreshold = Config::GetFloat("chargeThreshold2", 1.0f);
    g_killHp          = Config::GetFloat("killHp",          25.f);
    g_enabled         = Config::GetBool("enabled",          true);
    g_autoShoot       = Config::GetBool("autoShoot",        true);
    g_autoMelee       = Config::GetBool("autoMelee",        true);
    g_meleeRange      = Config::GetFloat("meleeRange",      2.f);
    g_meleeHoldSet    = Config::GetFloat("meleeHold",       0.10f);
    g_hitboxScale     = Config::GetFloat("hitboxScale",     1.0f);
    g_boneEnabled[0]  = Config::GetBool("boneHead",         true);
    g_boneEnabled[1]  = Config::GetBool("boneNeck",         true);
    g_boneEnabled[2]  = Config::GetBool("boneChest",        true);
    g_boneEnabled[3]  = Config::GetBool("boneBody",         true);
    g_boneEnabled[4]  = Config::GetBool("bonePelvis",       true);
    g_boneEnabled[5]  = Config::GetBool("boneLShoulder",    false);
    g_boneEnabled[6]  = Config::GetBool("boneRShoulder",    false);
    uint32_t heroLo   = (uint32_t)Config::GetInt("heroId_lo", 0);
    uint32_t heroHi   = (uint32_t)Config::GetInt("heroId_hi", 0);
    g_heroId          = ((uint64_t)heroHi << 32) | heroLo;
    g_heroLock        = (g_heroId != 0);
}
extern "C" void on_unload()
{
    Config::SetFloat("stiffness",       g_stiffness);
    Config::SetFloat("fovRadius",       g_fovRadius);
    Config::SetInt("targetMode",        g_targetMode);
    g_triggerKey.Save("triggerKey");
    Config::SetFloat("chargeThreshold2", g_chargeThreshold);
    Config::SetFloat("killHp",          g_killHp);
    Config::SetBool("enabled",          g_enabled);
    Config::SetBool("autoShoot",        g_autoShoot);
    Config::SetBool("autoMelee",        g_autoMelee);
    Config::SetFloat("meleeRange",      g_meleeRange);
    Config::SetFloat("meleeHold",       g_meleeHoldSet);
    Config::SetFloat("hitboxScale",     g_hitboxScale);
    Config::SetBool("boneHead",         g_boneEnabled[0]);
    Config::SetBool("boneNeck",         g_boneEnabled[1]);
    Config::SetBool("boneChest",        g_boneEnabled[2]);
    Config::SetBool("boneBody",         g_boneEnabled[3]);
    Config::SetBool("bonePelvis",       g_boneEnabled[4]);
    Config::SetBool("boneLShoulder",    g_boneEnabled[5]);
    Config::SetBool("boneRShoulder",    g_boneEnabled[6]);
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
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Illari) return; }  // dormant on any other hero

    g_triggerKey.Update();

    if (!g_triggerKey.IsDown() || g_cachedTarget < 0)
    {
        if (!g_triggerKey.IsDown())
        {
            g_cachedTarget = -1;
            AimResetSmoothing();
            if (g_autoShoot) ReleaseGameButton(GameButton::LMouse);
            ReleaseGameButton(GameButton::Melee);
            g_meleeHold = 0.f;
        }
        return;
    }

    // World distance to target
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

    float charge  = GetIllariCharge();
    bool  charged = charge >= g_chargeThreshold;
    bool  canKill = g_targetHp > 0.f && g_targetHp <= g_killHp;
    bool  fire    = charged || canKill;

    if (g_autoMelee && g_meleeHold <= 0.f && dist <= g_meleeRange)
        g_meleeHold = g_meleeHoldSet;

    if (g_meleeHold > 0.f)
    {
        PressGameButton(GameButton::Melee);
        g_meleeHold -= dt;
    }
    else
    {
        ReleaseGameButton(GameButton::Melee);
    }

    AimAtBone(g_cachedTarget, g_bestBone, g_stiffness);

    if (g_autoShoot)
    {
        if (fire && g_meleeHold <= 0.f && g_cachedHitbox >= 0)
            PressGameButton(GameButton::LMouse);
        else
            ReleaseGameButton(GameButton::LMouse);
    }
}

extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Illari) return; }  // dormant on any other hero

    bool held = g_triggerKey.IsDown();

    // Fast path: trigger not held — skip all per-player work
    if (!held)
    {
        g_targetValid  = false;
        g_cachedTarget = -1;
        g_cachedHitbox = -1;
        return;
    }

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    float cx = sz.x * 0.5f;
    float cy = sz.y * 0.5f;
    Vector2 center = { cx, cy };

    int32_t bestIdx     = -1;
    float   bestScore   = 99999.f;
    float   bestHp      = 0.f;
    Entity  bestEntity;
    bool    lockedAlive = false;
    float   lockedHp    = 0.f;
    Entity  lockedEntity;

    for (Entity p : Players())
    {
        if (!p.IsAlive() || p.IsLocal() || !p.IsVisible()) continue;

        Vector3 headWorld = p.GetHeadPos();
        if (!headWorld.IsValid()) continue;

        Vector2 headScreen;
        if (!WorldToScreen(headWorld, headScreen)) continue;

        if (p.Index() == g_cachedTarget)
        {
            lockedAlive  = true;
            lockedHp     = p.GetHealth();
            lockedEntity = p;
            continue;
        }

        float sd = ScreenDist(headScreen, center);
        if (sd > g_fovRadius) continue;
        float score = (g_targetMode == 1) ? p.GetHealth() : sd;
        if (score < bestScore) { bestScore = score; bestIdx = p.Index(); bestHp = p.GetHealth(); bestEntity = p; }
    }

    Entity tgt;
    if (lockedAlive) { bestIdx = g_cachedTarget; bestHp = lockedHp; tgt = lockedEntity; }
    else if (bestIdx >= 0) { tgt = bestEntity; }

    if (bestIdx >= 0)
    {
        if (bestIdx != g_cachedTarget) AimResetSmoothing();
        g_targetValid  = true;
        g_targetHp     = bestHp;
        g_cachedTarget = bestIdx;

        float closestBoneDist = 99999.f;
        g_bestBone = Bone::Chest;
        for (int b = 0; b < kAimBoneCount; b++)
        {
            if (!g_boneEnabled[b]) continue;
            Vector3 bWorld = tgt.GetBonePos(kAimBones[b]);
            if (!bWorld.IsValid()) continue;
            Vector2 bScreen;
            if (!WorldToScreen(bWorld, bScreen)) continue;
            float d = ScreenDist(bScreen, center);
            if (d < closestBoneDist) { closestBoneDist = d; g_bestBone = kAimBones[b]; }
        }

        g_cachedHitbox = AimHitsHitbox(g_cachedTarget, g_hitboxScale);
    }
    else
    {
        g_targetValid  = false;
        g_cachedTarget = -1;
        g_cachedHitbox = -1;
    }
}

extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Illari"))
    {
        ImGui::Checkbox("Enabled",    &g_enabled);
        if (!g_enabled) return;
        ImGui::Checkbox("Auto Shoot", &g_autoShoot);
        ImGui::Checkbox("Auto Melee", &g_autoMelee);
        ImGui::Separator();
        ImGui::SliderFloat("Smoothing (0=snap)", &g_stiffness,       0.f,   1500.f);
        ImGui::SliderFloat("FOV Radius",       &g_fovRadius,       10.f,  500.f);
        ImGui::Combo("Target Mode", &g_targetMode, "Closest Crosshair\0Lowest HP\0");
        g_triggerKey.Render("Trigger Key");
        ImGui::SliderFloat("Charge Threshold", &g_chargeThreshold, 0.f,   1.0f);
        ImGui::SliderFloat("Kill HP",          &g_killHp,          0.f,   200.f);
        ImGui::SliderFloat("Hitbox Scale",     &g_hitboxScale,     0.7f,  1.5f);
        ImGui::SliderFloat("Melee Range (m)", &g_meleeRange,      0.5f,  5.f);
        ImGui::SliderFloat("Melee Hold",      &g_meleeHoldSet,    0.05f, 0.3f);
        ImGui::Separator();
        ImGui::Text("Aim Bones:");
        for (int b = 0; b < kAimBoneCount; b++)
            ImGui::Checkbox(kBoneNames[b], &g_boneEnabled[b]);
        ImGui::Separator();
        TextBuilder<48> hb;
        hb.put("Hero ID: ").putInt((int)g_heroId).put(g_heroId == 0 ? "  (any)" : "  (locked)");
        ImGui::Text(hb.c_str());
        bool prevLock = g_heroLock;
        ImGui::Checkbox("Lock to current hero", &g_heroLock);
        if (g_heroLock != prevLock)
        {
            if (g_heroLock) g_heroId = LocalPlayer().GetHeroId();
            else            g_heroId = 0;
        }
    }
}
