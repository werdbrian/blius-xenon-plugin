// junker_queen.cpp
// Auto shout (low HP), auto shoot (key), auto blade (key + conditions), auto melee (proximity)

#include <xenon/SDK.hpp>
using namespace xenon;

// ── Config ────────────────────────────────────────────────
static bool  g_enabled          = true;

// Aim assist
static float g_stiffness        = 30.f;
static float g_fovRadius        = 200.f;
static float g_hitboxScale      = 1.0f;

// Auto shout
static bool  g_autoShout        = true;
static float g_shoutHpThreshold = 50.f;  // % HP

// Auto shoot
static bool  g_autoShoot        = true;
static int   g_shootKey         = 18;    // VK 18 = Left Alt

// Auto blade (RMouse)
static bool  g_autoBlade        = true;
static int   g_bladeKey         = 18;    // VK 18 = Left Alt
static float g_bladeHpThreshold = 100.f; // fire if enemy HP <= this
static float g_bladeRange       = 20.f;  // max distance (m)
static float g_bladeArcFactor   = 0.1f;   // height offset: dist * factor (linear)
static float g_bladeStiffness   = 200.f;

// Blade combo: throw -> detect hit -> recall (pull) -> Carnage (E)
static bool  g_bladeCombo       = true;
static float g_bladeHitHpDrop   = 30.f;  // HP drop within window counts as "blade hit"
static float g_carnageRange     = 4.f;   // distance to fire Carnage after pull

// Combo state machine
enum BladePhase { BP_IDLE, BP_THROWN, BP_HIT, BP_RECALL, BP_PULLING, BP_CARNAGE };
static BladePhase g_bladePhase    = BP_IDLE;
static float      g_bladePhaseT   = 0.f;
static float      g_bladeStartHp  = 0.f;

// Auto melee
static bool  g_autoMelee        = true;
static float g_meleeRange       = 3.f;
static float g_meleeHold        = 0.f;
static float g_meleeHoldSet     = 0.10f;

// Aim bones
static const int kAimBones[]    = { Bone::Head, Bone::Neck, Bone::Chest, Bone::Body, Bone::Pelvis };
static const char* kBoneNames[] = { "Head", "Neck", "Chest", "Body", "Pelvis" };
static const int kBoneCount     = 5;
static bool g_boneEnabled[5]    = { true, true, true, true, true };

// Hero lock
static uint64_t g_heroId  = 0;
static bool     g_heroLock = false;

// Blade projectile speed for prediction (m/s) — tune in menu
static float   g_bladeProjectileSpeed = 40.f;

// Cached from on_render
static int32_t g_cachedTarget    = -1;
static bool    g_targetValid     = false;
static float   g_targetHp        = 0.f;
static float   g_targetDist      = 9999.f;
static int     g_cachedHitbox    = -1;
static int     g_bestBone        = Bone::Chest;
static Vector3 g_targetHeadPos   = {};
static Vector3 g_targetVelocity  = {};
static Vector3 g_prevHeadPos     = {};
static float   g_prevHeadTime    = 0.f;

// ── Helpers ───────────────────────────────────────────────
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

// ── Plugin Info ───────────────────────────────────────────
XENON_PLUGIN_INFO(
    "junker_queen", "Junker Queen", "Xenon",
    "Auto shout, shoot, blade, melee.",
    "1.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ── Lifecycle ─────────────────────────────────────────────
extern "C" void on_load()
{
    g_enabled          = Config::GetBool("enabled",          true);
    g_stiffness        = Config::GetFloat("stiffness",       30.f);
    g_fovRadius        = Config::GetFloat("fovRadius",       200.f);
    g_hitboxScale      = Config::GetFloat("hitboxScale",     1.0f);
    g_autoShout        = Config::GetBool("autoShout",        true);
    g_shoutHpThreshold = Config::GetFloat("shoutHpThresh",   50.f);
    g_autoShoot        = Config::GetBool("autoShoot",        true);
    g_shootKey         = Config::GetInt("shootKey",          18);
    g_autoBlade        = Config::GetBool("autoBlade",        true);
    g_bladeKey         = Config::GetInt("bladeKey",          18);
    g_bladeHpThreshold = Config::GetFloat("bladeHpThresh",   100.f);
    g_bladeRange       = Config::GetFloat("bladeRange",      20.f);
    g_bladeArcFactor   = Config::GetFloat("bladeArcFactor",  0.1f);
    g_bladeStiffness   = Config::GetFloat("bladeStiffness",  200.f);
    g_bladeCombo            = Config::GetBool("bladeCombo",           true);
    g_bladeHitHpDrop        = Config::GetFloat("bladeHitHpDrop",       30.f);
    g_carnageRange          = Config::GetFloat("carnageRange",          4.f);
    g_bladeProjectileSpeed  = Config::GetFloat("bladeProjectileSpeed",  40.f);
    g_autoMelee        = Config::GetBool("autoMelee",        true);
    g_meleeRange       = Config::GetFloat("meleeRange",      3.f);
    g_meleeHoldSet     = Config::GetFloat("meleeHold",       0.10f);
    g_boneEnabled[0]   = Config::GetBool("boneHead",         true);
    g_boneEnabled[1]   = Config::GetBool("boneNeck",         true);
    g_boneEnabled[2]   = Config::GetBool("boneChest",        true);
    g_boneEnabled[3]   = Config::GetBool("boneBody",         true);
    g_boneEnabled[4]   = Config::GetBool("bonePelvis",       true);
    uint32_t lo        = (uint32_t)Config::GetInt("heroId_lo", 0);
    uint32_t hi        = (uint32_t)Config::GetInt("heroId_hi", 0);
    g_heroId           = ((uint64_t)hi << 32) | lo;
    g_heroLock         = (g_heroId != 0);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",        g_enabled);
    Config::SetFloat("stiffness",     g_stiffness);
    Config::SetFloat("fovRadius",     g_fovRadius);
    Config::SetFloat("hitboxScale",   g_hitboxScale);
    Config::SetBool("autoShout",      g_autoShout);
    Config::SetFloat("shoutHpThresh", g_shoutHpThreshold);
    Config::SetBool("autoShoot",      g_autoShoot);
    Config::SetInt("shootKey",        g_shootKey);
    Config::SetBool("autoBlade",      g_autoBlade);
    Config::SetInt("bladeKey",        g_bladeKey);
    Config::SetFloat("bladeHpThresh", g_bladeHpThreshold);
    Config::SetFloat("bladeRange",    g_bladeRange);
    Config::SetFloat("bladeArcFactor",  g_bladeArcFactor);
    Config::SetFloat("bladeStiffness",  g_bladeStiffness);
    Config::SetBool("bladeCombo",             g_bladeCombo);
    Config::SetFloat("bladeHitHpDrop",        g_bladeHitHpDrop);
    Config::SetFloat("carnageRange",          g_carnageRange);
    Config::SetFloat("bladeProjectileSpeed",  g_bladeProjectileSpeed);
    Config::SetBool("autoMelee",      g_autoMelee);
    Config::SetFloat("meleeRange",    g_meleeRange);
    Config::SetFloat("meleeHold",     g_meleeHoldSet);
    Config::SetBool("boneHead",       g_boneEnabled[0]);
    Config::SetBool("boneNeck",       g_boneEnabled[1]);
    Config::SetBool("boneChest",      g_boneEnabled[2]);
    Config::SetBool("boneBody",       g_boneEnabled[3]);
    Config::SetBool("bonePelvis",     g_boneEnabled[4]);
    Config::SetInt("heroId_lo", (int32_t)(g_heroId & 0xFFFFFFFF));
    Config::SetInt("heroId_hi", (int32_t)(g_heroId >> 32));
    Config::Save();
}

extern "C" void on_hero_changed(uint64_t) { g_cachedTarget = -1; AimResetSmoothing(); }

// ── Frame Logic ───────────────────────────────────────────
extern "C" void on_frame(float dt)
{
    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;

    bool shootHeld = IsKeyDown(g_shootKey);
    bool bladeHeld = IsKeyDown(g_bladeKey);

    // Auto shout — passive, no key required
    if (g_autoShout)
    {
        float hpPct = local.GetHealthPercent();
        SkillCooldown shout = local.GetSkill1Cooldown();
        if (hpPct > 0.f && hpPct <= g_shoutHpThreshold && !shout.IsOnCooldown())
            PressGameButton(GameButton::Skill1);
        else
            ReleaseGameButton(GameButton::Skill1);
    }

    // Release inputs when no key held and no target
    if (!shootHeld && !bladeHeld)
    {
        if (g_autoShoot) ReleaseGameButton(GameButton::LMouse);
        if (g_autoBlade) ReleaseGameButton(GameButton::RMouse);
        if (g_autoMelee) { ReleaseGameButton(GameButton::Melee); g_meleeHold = 0.f; }
        if (!g_targetValid) AimResetSmoothing();
        return;
    }

    if (!g_targetValid || g_cachedTarget < 0) return;

    // Blade takes priority over shoot aim when conditions are met
    bool bladeConditions = g_autoBlade && bladeHeld
        && g_targetHp <= g_bladeHpThreshold
        && g_targetDist <= g_bladeRange
        && g_targetHeadPos.IsValid();

    // Bail out of combo if blade key released or target lost
    if (g_bladePhase != BP_IDLE && (!bladeHeld || !g_targetValid))
    {
        g_bladePhase = BP_IDLE; g_bladePhaseT = 0.f;
        ReleaseGameButton(GameButton::RMouse);
        ReleaseGameButton(GameButton::Skill2);
    }

    if (g_bladeCombo && bladeConditions)
    {
        // Aim with arc + projectile prediction
        float   ttt    = (g_bladeProjectileSpeed > 0.f) ? g_targetDist / g_bladeProjectileSpeed : 0.f;
        Vector3 arcPos = g_targetHeadPos;
        arcPos.x += g_targetVelocity.x * ttt;
        arcPos.y += g_targetVelocity.y * ttt + g_targetDist * g_bladeArcFactor;
        arcPos.z += g_targetVelocity.z * ttt;
        AimAtPosition(arcPos, g_bladeStiffness);

        g_bladePhaseT += dt;
        switch (g_bladePhase)
        {
        case BP_IDLE:
            // Throw the blade — record HP at throw time so we can detect a hit.
            g_bladeStartHp = g_targetHp;
            g_bladePhase   = BP_THROWN;
            g_bladePhaseT  = 0.f;
            PressGameButton(GameButton::RMouse);
            break;

        case BP_THROWN:
            // Keep RMB held for a few frames so the throw registers, then release.
            // Watch target HP — a drop means the blade landed.
            if (g_bladePhaseT > 0.12f) ReleaseGameButton(GameButton::RMouse);
            if (g_targetHp <= g_bladeStartHp - g_bladeHitHpDrop)
            { g_bladePhase = BP_HIT; g_bladePhaseT = 0.f; }
            else if (g_bladePhaseT > 1.5f)
            { g_bladePhase = BP_IDLE; g_bladePhaseT = 0.f; }  // missed
            break;

        case BP_HIT:
            // Small gap before re-pressing RMB to recall.
            ReleaseGameButton(GameButton::RMouse);
            if (g_bladePhaseT > 0.08f)
            { g_bladePhase = BP_RECALL; g_bladePhaseT = 0.f; }
            break;

        case BP_RECALL:
            // Press RMB again — this triggers blade recall (pulls them in).
            PressGameButton(GameButton::RMouse);
            if (g_bladePhaseT > 0.12f)
            { ReleaseGameButton(GameButton::RMouse); g_bladePhase = BP_PULLING; g_bladePhaseT = 0.f; }
            break;

        case BP_PULLING:
            // Fire E when in range OR after pull timer — whichever comes first
            if (g_targetDist <= g_carnageRange || g_bladePhaseT > 0.5f)
            { g_bladePhase = BP_CARNAGE; g_bladePhaseT = 0.f; }
            else if (g_bladePhaseT > 2.0f)
            { g_bladePhase = BP_IDLE; g_bladePhaseT = 0.f; }  // pull failed
            break;

        case BP_CARNAGE:
            // Fire Skill2 (Carnage / axe swing) — edge-triggered, short press.
            PressGameButton(GameButton::Skill2);
            if (g_bladePhaseT > 0.12f)
            { ReleaseGameButton(GameButton::Skill2); g_bladePhase = BP_IDLE; g_bladePhaseT = 0.f; }
            break;
        }
    }
    else if (bladeConditions)
    {
        // Simple hold: same arc + prediction
        float   ttt    = (g_bladeProjectileSpeed > 0.f) ? g_targetDist / g_bladeProjectileSpeed : 0.f;
        Vector3 arcPos = g_targetHeadPos;
        arcPos.x += g_targetVelocity.x * ttt;
        arcPos.y += g_targetVelocity.y * ttt + g_targetDist * g_bladeArcFactor;
        arcPos.z += g_targetVelocity.z * ttt;
        AimAtPosition(arcPos, g_bladeStiffness);
        PressGameButton(GameButton::RMouse);
    }
    else
    {
        ReleaseGameButton(GameButton::RMouse);
        if (shootHeld)
            AimAtBone(g_cachedTarget, g_bestBone, g_stiffness);
    }

    // Auto shoot — suppressed when blade is ready to throw (blade takes priority)
    bool bladeReady = bladeConditions && g_bladePhase == BP_IDLE;
    if (g_autoShoot && shootHeld)
    {
        if (g_cachedHitbox >= 0 && g_meleeHold <= 0.f && !bladeReady)
            PressGameButton(GameButton::LMouse);
        else
            ReleaseGameButton(GameButton::LMouse);
    }

    // Auto melee — passive proximity (uses hold timer like Illari)
    if (g_autoMelee && g_meleeHold <= 0.f && g_targetDist <= g_meleeRange)
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
}

// ── Render (target caching + HUD) ────────────────────────
extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    bool shootHeld = IsKeyDown(g_shootKey);
    bool bladeHeld = IsKeyDown(g_bladeKey);

    if (!shootHeld && !bladeHeld)
    {
        g_targetValid  = false;
        g_cachedTarget = -1;
        g_cachedHitbox = -1;
        return;
    }

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;
    Vector2 center = { sz.x * 0.5f, sz.y * 0.5f };

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;

    int32_t bestIdx   = -1;
    float   bestScore = 99999.f;
    float   bestHp    = 0.f;
    float   bestDist  = 9999.f;
    Entity  bestEnt;
    bool    lockedAlive = false;
    Entity  lockedEnt;

    for (Entity p : Players())
    {
        if (!p.IsAlive() || p.IsLocal() || !p.IsVisible()) continue;

        Vector3 headWorld = p.GetBonePos(Bone::Head);
        if (!headWorld.IsValid()) continue;

        Vector2 headScreen;
        if (!WorldToScreen(headWorld, headScreen)) continue;

        float sd = ScreenDist(headScreen, center);
        if (sd > g_fovRadius) continue;

        float dist = WorldDist(local.GetPosition(), p.GetPosition());

        if (p.Index() == g_cachedTarget)
        {
            lockedAlive = true;
            bestHp      = p.GetHealth();
            bestDist    = dist;
            lockedEnt   = p;
            continue;
        }

        if (sd < bestScore) { bestScore = sd; bestIdx = p.Index(); bestHp = p.GetHealth(); bestDist = dist; bestEnt = p; }
    }

    Entity tgt;
    if (lockedAlive) { bestIdx = g_cachedTarget; tgt = lockedEnt; }
    else if (bestIdx >= 0) { tgt = bestEnt; }

    if (bestIdx >= 0)
    {
        if (bestIdx != g_cachedTarget) { AimResetSmoothing(); g_targetVelocity = {}; g_prevHeadTime = 0.f; }
        g_targetValid  = true;
        g_targetHp     = bestHp;
        g_targetDist   = bestDist;
        g_cachedTarget = bestIdx;

        // Best aim bone
        float closestBone = 99999.f;
        g_bestBone = Bone::Chest;
        for (int b = 0; b < kBoneCount; b++)
        {
            if (!g_boneEnabled[b]) continue;
            Vector3 bw = tgt.GetBonePos(kAimBones[b]);
            if (!bw.IsValid()) continue;
            Vector2 bs;
            if (!WorldToScreen(bw, bs)) continue;
            float d = ScreenDist(bs, center);
            if (d < closestBone) { closestBone = d; g_bestBone = kAimBones[b]; }
        }

        g_cachedHitbox  = AimHitsHitbox(g_cachedTarget, g_hitboxScale);
        g_targetHeadPos = tgt.GetBonePos(Bone::Head);

        // Track head velocity for blade prediction
        float now = GetTime();
        if (g_prevHeadTime > 0.f)
        {
            float invDt = 1.f / (now - g_prevHeadTime);
            g_targetVelocity.x = (g_targetHeadPos.x - g_prevHeadPos.x) * invDt;
            g_targetVelocity.y = (g_targetHeadPos.y - g_prevHeadPos.y) * invDt;
            g_targetVelocity.z = (g_targetHeadPos.z - g_prevHeadPos.z) * invDt;
        }
        g_prevHeadPos  = g_targetHeadPos;
        g_prevHeadTime = now;
    }
    else
    {
        g_targetValid    = false;
        g_cachedTarget   = -1;
        g_cachedHitbox   = -1;
        g_targetVelocity = {};
        g_prevHeadTime   = 0.f;
    }

    // HUD
    float x = 10.f, y = 200.f;
    const float F = 11.f, L = 14.f;

    if (g_targetValid)
    {
        TextBuilder<48> tb;
        tb.put("TARGET hp=").putFloat(g_targetHp, 0).put(" dist=").putFloat(g_targetDist, 1);
        Draw::TextShadow(x, y, Color::Green(), tb.c_str(), F); y += L;

        tb.clear(); tb.put("aim=").putInt(g_cachedHitbox).put(" melee=").putInt(g_targetDist <= g_meleeRange);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;
    }

    // Skill state panel — observe which field changes when blade is thrown/embedded
    Entity lp = LocalPlayer();
    if (lp.IsValid())
    {
        TextBuilder<48> tb;
        Draw::TextShadow(x, y, Color::Yellow(), "-- SKILL STATE --", F); y += L;

        tb.clear(); tb.put("s1Active=").putInt(lp.IsSkill1Active())
                       .put(" s2Active=").putInt(lp.IsSkill2Active())
                       .put(" s3Active=").putInt(lp.IsSkill3Active());
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        SkillCooldown s1cd = lp.GetSkill1Cooldown();
        tb.clear(); tb.put("s1cd=").putFloat(s1cd.current, 1).put("/").putFloat(s1cd.max, 1);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        SkillCooldown s2cd = lp.GetSkill2Cooldown();
        tb.clear(); tb.put("s2cd=").putFloat(s2cd.current, 1).put("/").putFloat(s2cd.max, 1);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        SkillCooldown s3cd = lp.GetSkill3Cooldown();
        tb.clear(); tb.put("s3cd=").putFloat(s3cd.current, 1).put("/").putFloat(s3cd.max, 1);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        SkillCooldown s1dur = lp.GetSkill1Duration();
        tb.clear(); tb.put("s1dur=").putFloat(s1dur.current, 2);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        SkillCooldown s2dur = lp.GetSkill2Duration();
        tb.clear(); tb.put("s2dur=").putFloat(s2dur.current, 2);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        tb.clear(); tb.put("ultActive=").putInt(lp.IsUltActive())
                       .put(" ultChg=").putFloat(lp.GetUltCharge(), 2);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F);
    }
}

// ── Menu ──────────────────────────────────────────────────
extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled", &g_enabled);
    if (!g_enabled) return;
    ImGui::Separator();

    ImGui::Checkbox("Auto Shout (low HP)", &g_autoShout);
    if (g_autoShout)
        ImGui::SliderFloat("Shout HP %", &g_shoutHpThreshold, 10.f, 90.f);
    ImGui::Separator();

    ImGui::Checkbox("Auto Shoot", &g_autoShoot);
    if (g_autoShoot)
    {
        ImGui::SliderInt("Shoot Key (VK)", &g_shootKey, 0, 255);
        ImGui::SliderFloat("Smoothing", &g_stiffness, 0.f, 1500.f);
        ImGui::SliderFloat("FOV Radius", &g_fovRadius, 10.f, 500.f);
        ImGui::SliderFloat("Hitbox Scale", &g_hitboxScale, 0.5f, 2.f);
    }
    ImGui::Separator();

    ImGui::Checkbox("Auto Blade", &g_autoBlade);
    if (g_autoBlade)
    {
        ImGui::SliderInt("Blade Key (VK)", &g_bladeKey, 0, 255);
        ImGui::SliderFloat("Blade HP Threshold", &g_bladeHpThreshold, 1.f, 500.f);
        ImGui::SliderFloat("Blade Range (m)", &g_bladeRange, 1.f, 40.f);
        ImGui::SliderFloat("Blade Arc Factor",        &g_bladeArcFactor,          0.f,  0.5f);
        ImGui::SliderFloat("Blade Projectile Speed",  &g_bladeProjectileSpeed,    10.f, 100.f);
        ImGui::SliderFloat("Blade Smoothing", &g_bladeStiffness, 0.f, 5000.f);
        ImGui::Checkbox("Blade Combo (auto recall + Carnage)", &g_bladeCombo);
        if (g_bladeCombo)
        {
            ImGui::SliderFloat("Blade Hit HP Drop", &g_bladeHitHpDrop, 1.f, 200.f);
            ImGui::SliderFloat("Carnage Range (m)", &g_carnageRange, 1.f, 8.f);
        }
    }
    ImGui::Separator();

    ImGui::Checkbox("Auto Melee", &g_autoMelee);
    if (g_autoMelee)
        ImGui::SliderFloat("Melee Range (m)", &g_meleeRange, 0.5f, 6.f);
    ImGui::Separator();

    ImGui::Text("Aim Bones:");
    for (int b = 0; b < kBoneCount; b++)
        ImGui::Checkbox(kBoneNames[b], &g_boneEnabled[b]);
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
