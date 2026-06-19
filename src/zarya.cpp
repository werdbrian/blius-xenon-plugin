// zarya.cpp
// Primary beam aim assist + secondary orb prediction + auto barrier on incoming damage

#include <xenon/SDK.hpp>
#include "projectile_pred.hpp"
using namespace xenon;

// ── Config ────────────────────────────────────────────────
static bool  g_enabled          = true;

// Aim
static float g_stiffness        = 30.f;
static float g_fovRadius        = 200.f;
static float g_hitboxScale      = 1.0f;
static int   g_triggerKey       = 18;    // VK 18 = Left Alt — beam key
static int   g_orbKey           = 0;     // separate orb key (0 = same as beam key)

// Auto primary (beam — hitscan, no prediction needed)
static bool  g_autoPrimary      = true;

// Auto secondary (orb — slow projectile, needs prediction)
static bool  g_autoSecondary    = true;
static float g_secArcFactor     = 0.04f; // weapon gravity drop: flightTime² * factor
static float g_secRange         = 30.f;  // don't fire secondary beyond this (m)
static float g_secSpeed         = 25.f;  // fallback orb speed if SDK returns 0

// Auto primary range (hold LMB when target within this distance)
static float g_primaryRange     = 15.f;

// Auto barrier (Skill1/Shift) — triggers when taking heavy incoming damage
static bool  g_autoBarrier      = true;
static float g_barrierDmgRate   = 60.f;  // HP/sec loss to trigger (tune to taste)

// Auto melee
static bool  g_autoMelee        = true;
static float g_meleeRange       = 3.f;
static float g_meleeHold        = 0.f;
static float g_meleeHoldSet     = 0.10f;

// Aim bones
static const int   kAimBones[]  = { Bone::Head, Bone::Neck, Bone::Chest, Bone::Body, Bone::Pelvis };
static const char* kBoneNames[] = { "Head", "Neck", "Chest", "Body", "Pelvis" };
static const int   kBoneCount   = 5;
static bool g_boneEnabled[5]    = { true, true, true, true, true };

// Hero lock
static uint64_t g_heroId  = 0;
static bool     g_heroLock = false;

// Cached from on_render
static int32_t g_cachedTarget = -1;
static bool    g_targetValid  = false;
static float   g_targetHp     = 0.f;
static float   g_targetDist   = 9999.f;
static int     g_cachedHitbox = -1;
static int     g_bestBone     = Bone::Chest;

// Cached from on_frame for visualization
static ProjPred::Result g_lastPred{};
static Vector3 g_smoothedPredPos{};
static Vector3 g_predTargetPos{};    // raw target position at flight time (before arc offset)
static bool    g_predInited = false;  // snap once on first valid prediction
static float   g_switchCooldown   = 0.f;
static int     g_predFreezeFrames = 0;  // hold smoothed pos steady during knockback spikes
static int     g_lockMissingFrames = 0; // frames locked target hasn't been seen (dropout tolerance)

// Damage rate tracking for auto barrier
static float g_prevHp         = -1.f;
static float g_hpDropRate     = 0.f;  // HP lost per second this frame

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
    "zarya", "Zarya", "Xenon",
    "Beam aim assist, predicted orb, auto barrier.",
    "1.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ── Lifecycle ─────────────────────────────────────────────
extern "C" void on_load()
{
    g_enabled         = Config::GetBool("enabled",         true);
    g_stiffness       = Config::GetFloat("stiffness",      30.f);
    g_fovRadius       = Config::GetFloat("fovRadius",      200.f);
    g_hitboxScale     = Config::GetFloat("hitboxScale",    1.0f);
    g_triggerKey      = Config::GetInt("triggerKey",       18);
    g_orbKey          = Config::GetInt("orbKey",           0);
    g_autoPrimary     = Config::GetBool("autoPrimary",     true);
    g_autoSecondary   = Config::GetBool("autoSecondary",   true);
    g_secArcFactor    = Config::GetFloat("secArcFactor",   0.04f);
    g_secRange        = Config::GetFloat("secRange",       30.f);
    g_secSpeed        = Config::GetFloat("secSpeed",       25.f);
    g_primaryRange    = Config::GetFloat("primaryRange",   15.f);
    g_autoBarrier     = Config::GetBool("autoBarrier",     true);
    g_barrierDmgRate  = Config::GetFloat("barrierDmgRate", 60.f);
    g_autoMelee       = Config::GetBool("autoMelee",       true);
    g_meleeRange      = Config::GetFloat("meleeRange",     3.f);
    g_meleeHoldSet    = Config::GetFloat("meleeHold",      0.10f);
    g_boneEnabled[0]  = Config::GetBool("boneHead",        true);
    g_boneEnabled[1]  = Config::GetBool("boneNeck",        true);
    g_boneEnabled[2]  = Config::GetBool("boneChest",       true);
    g_boneEnabled[3]  = Config::GetBool("boneBody",        true);
    g_boneEnabled[4]  = Config::GetBool("bonePelvis",      true);
    uint32_t lo       = (uint32_t)Config::GetInt("heroId_lo", 0);
    uint32_t hi       = (uint32_t)Config::GetInt("heroId_hi", 0);
    g_heroId          = ((uint64_t)hi << 32) | lo;
    g_heroLock        = (g_heroId != 0);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",        g_enabled);
    Config::SetFloat("stiffness",     g_stiffness);
    Config::SetFloat("fovRadius",     g_fovRadius);
    Config::SetFloat("hitboxScale",   g_hitboxScale);
    Config::SetInt("triggerKey",      g_triggerKey);
    Config::SetInt("orbKey",          g_orbKey);
    Config::SetBool("autoPrimary",    g_autoPrimary);
    Config::SetBool("autoSecondary",  g_autoSecondary);
    Config::SetFloat("secArcFactor",  g_secArcFactor);
    Config::SetFloat("secRange",      g_secRange);
    Config::SetFloat("secSpeed",      g_secSpeed);
    Config::SetFloat("primaryRange",  g_primaryRange);
    Config::SetBool("autoBarrier",    g_autoBarrier);
    Config::SetFloat("barrierDmgRate",g_barrierDmgRate);
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

extern "C" void on_hero_changed(uint64_t) { g_cachedTarget = -1; AimResetSmoothing(); g_prevHp = -1.f; }

// ── Frame Logic ───────────────────────────────────────────
extern "C" void on_frame(float dt)
{
    if (!g_enabled || !IsIngame()) return;
    if (g_heroId != 0 && GetCurrentHero() != g_heroId) return;

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;

    // Track incoming damage rate for auto barrier
    float hp = local.GetHealth();
    if (g_prevHp >= 0.f && dt > 0.f)
        g_hpDropRate = (g_prevHp - hp) / dt;
    else
        g_hpDropRate = 0.f;
    g_prevHp = hp;

    // Auto barrier — passive, triggers on heavy incoming damage
    if (g_autoBarrier)
    {
        SkillCooldown barrier = local.GetSkill1Cooldown();
        if (g_hpDropRate > g_barrierDmgRate && !barrier.IsOnCooldown())
            PressGameButton(GameButton::Skill1);
        else
            ReleaseGameButton(GameButton::Skill1);
    }

    bool beamHeld = IsKeyDown(g_triggerKey);
    bool orbHeld  = (g_orbKey > 0) ? IsKeyDown(g_orbKey) : beamHeld;
    if (g_switchCooldown > 0.f) g_switchCooldown -= dt;
    bool hasTarget = (beamHeld || orbHeld) && g_targetValid && g_cachedTarget >= 0;

    if (!beamHeld && !orbHeld)
    {
        if (g_autoPrimary)   ReleaseGameButton(GameButton::LMouse);
        if (g_autoSecondary) ReleaseGameButton(GameButton::RMouse);
        if (g_autoMelee)     { ReleaseGameButton(GameButton::Melee); g_meleeHold = 0.f; }
        if (!g_targetValid)  AimResetSmoothing();
        g_lastPred         = ProjPred::Result{};
        g_predInited       = false;
        g_predFreezeFrames = 0;
        return;
    }

    if (hasTarget)
    {
        // Orb aim: predicted position. Falls back to bone if prediction unavailable or out of range.
        // Never lets aim go uncontrolled — uncontrolled aim between frames = flick.
        bool wantOrb = g_autoSecondary && orbHeld && g_lastPred.valid && g_targetDist <= g_secRange;
        if (wantOrb)
            AimAtPosition(g_smoothedPredPos, g_stiffness);
        else if (beamHeld || orbHeld)
            AimAtBone(g_cachedTarget, g_bestBone, g_stiffness);
    }

    // Auto primary — hold LMB while beam key held
    if (g_autoPrimary)
    {
        if (beamHeld && hasTarget) PressGameButton(GameButton::LMouse);
        else                       ReleaseGameButton(GameButton::LMouse);
    }

    // Auto secondary — hold RMB while orb key held and in range
    if (g_autoSecondary)
    {
        if (orbHeld && hasTarget && g_targetDist <= g_secRange)
            PressGameButton(GameButton::RMouse);
        else
            ReleaseGameButton(GameButton::RMouse);
    }

    // Auto melee — fires alongside beam, no LMB interruption
    if (g_autoMelee && hasTarget)
    {
        if (g_meleeHold <= 0.f && g_targetDist <= g_meleeRange)
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

    bool held = IsKeyDown(g_triggerKey) || (g_orbKey > 0 && IsKeyDown(g_orbKey));

    if (!held)
    {
        g_targetValid       = false;
        g_cachedTarget      = -1;
        g_cachedHitbox      = -1;
        g_switchCooldown    = 0.f;
        g_lockMissingFrames = 0;
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
        if (p.IsLocal()) continue;
        if (p.GetEntityType() != EntityType::Hero && p.GetEntityType() != EntityType::Bot) continue;

        float dist = WorldDist(local.GetPosition(), p.GetPosition());

        // Check locked target BEFORE IsAlive — brief dropouts shouldn't break the lock
        if (p.Index() == g_cachedTarget)
        {
            if (p.IsAlive())
            {
                lockedAlive = true;
                bestHp      = p.GetHealth();
                bestDist    = dist;
                lockedEnt   = p;
            }
            continue;
        }

        // New target acquisition requires alive + visible + FOV
        if (!p.IsAlive()) continue;
        if (!p.IsVisible()) continue;
        if (!p.IsEnemy()) continue;

        Vector3 headWorld = p.GetBonePos(Bone::Head);
        if (!headWorld.IsValid()) continue;

        Vector2 headScreen;
        if (!WorldToScreen(headWorld, headScreen)) continue;

        float sd = ScreenDist(headScreen, center);
        if (sd > g_fovRadius) continue;

        if (!p.IsEnemy()) continue;
        if (sd < bestScore) { bestScore = sd; bestIdx = p.Index(); bestHp = p.GetHealth(); bestDist = dist; bestEnt = p; }
    }

    // If locked target is gone, tolerate a few frames before clearing the lock
    if (!lockedAlive && g_cachedTarget >= 0)
    {
        g_lockMissingFrames++;
        if (g_lockMissingFrames >= 4)
        {
            g_cachedTarget      = -1;
            g_switchCooldown    = 0.4f;
            g_predInited        = false;
            g_lockMissingFrames = 0;
            AimResetSmoothing();
        }
    }
    else
    {
        if (g_lockMissingFrames > 0) { g_predInited = false; g_predFreezeFrames = 0; }
        g_lockMissingFrames = 0;
    }

    // Block new target acquisition while cooldown is active
    if (!lockedAlive && g_switchCooldown > 0.f)
        bestIdx = -1;

    Entity tgt;
    if (lockedAlive) { bestIdx = g_cachedTarget; tgt = lockedEnt; }
    else if (bestIdx >= 0) { tgt = bestEnt; }

    if (bestIdx >= 0)
    {
        if (bestIdx != g_cachedTarget) { AimResetSmoothing(); g_predInited = false; g_predFreezeFrames = 0; }
        g_targetValid  = true;
        g_targetHp     = bestHp;
        g_targetDist   = bestDist;
        g_cachedTarget = bestIdx;

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

        g_cachedHitbox = AimHitsHitbox(g_cachedTarget, g_hitboxScale);
    }
    else
    {
        g_targetValid  = false;
        g_cachedTarget = -1;
        g_cachedHitbox = -1;
    }

    // Compute prediction here — tgt is the confirmed-valid entity for this frame
    g_lastPred = ProjPred::Result{};
    if (g_targetValid && tgt.IsValid())
    {
        WeaponInfo wi{};
        bool hasProjInfo = GetWeaponInfo(InputFlag::SecondaryFire, wi);
        float projSpeed = (hasProjInfo && wi.projectileSpeed > 0.f) ? wi.projectileSpeed : g_secSpeed;
        if (projSpeed > 0.f)
        {
            g_lastPred = ProjPred::Solve(tgt, local.GetPosition(), projSpeed);
            if (g_lastPred.valid)
            {
                g_predTargetPos = g_lastPred.aimPos; // where target will be (no arc)
                g_lastPred.aimPos.y += g_lastPred.flightTime * g_lastPred.flightTime * g_secArcFactor;

                if (!g_predInited)
                {
                    // First valid prediction this lock — snap directly, no pull from origin
                    g_smoothedPredPos = g_lastPred.aimPos;
                    g_predInited = true;
                }
                else
                {
                    float dx = g_lastPred.aimPos.x - g_smoothedPredPos.x;
                    float dy = g_lastPred.aimPos.y - g_smoothedPredPos.y;
                    float dz = g_lastPred.aimPos.z - g_smoothedPredPos.z;
                    float dist2 = dx*dx + dy*dy + dz*dz;
                    // >1m jump in one frame = knockback spike — freeze and let it settle
                    if (dist2 > 1.f * 1.f)
                    {
                        g_predFreezeFrames = 6; // ~95ms at 64Hz
                    }
                    if (g_predFreezeFrames > 0)
                    {
                        g_predFreezeFrames--;
                        // don't update smoothed pos
                    }
                    else
                    {
                        g_smoothedPredPos.x += dx;
                        g_smoothedPredPos.y += dy;
                        g_smoothedPredPos.z += dz;
                    }
                }
            }
            else
            {
                g_smoothedPredPos = {};
                g_predInited = false;
            }
        }
    }

    // Draw target indicator — cyan circle + crosshair lines on the target's head
    if (g_targetValid && tgt.IsValid())
    {
        Vector3 headW = tgt.GetBonePos(Bone::Head);
        Vector2 headS;
        if (headW.IsValid() && WorldToScreen(headW, headS))
        {
            Draw::Circle(headS, 10.f, Color::Cyan(), 1.5f);
            Draw::Line(headS.x - 14.f, headS.y, headS.x + 14.f, headS.y, Color::Cyan(), 1.f);
            Draw::Line(headS.x, headS.y - 14.f, headS.x, headS.y + 14.f, Color::Cyan(), 1.f);
        }
    }

    // Draw predicted orb impact — orange dot (aim point) + magenta dot (target position at T)
    if (g_lastPred.valid)
    {
        Vector2 predS;
        if (WorldToScreen(g_smoothedPredPos, predS))
        {
            Draw::Line(center.x, center.y, predS.x, predS.y, Color(255, 165, 0, 100), 1.f);
            Draw::CircleFilled(predS, 5.f, Color(255, 165, 0));
            Draw::Circle(predS, 5.f, Color::White(), 1.f);
            if (g_lastPred.aimsAtLanding)
                Draw::TextCentered(predS.x, predS.y + 10.f, Color(255, 165, 0), "LAND", 9);
        }

        // Magenta dot: where the target's body will be when the orb arrives
        Vector2 tgtS;
        if (WorldToScreen(g_predTargetPos, tgtS))
        {
            Draw::CircleFilled(tgtS, 5.f, Color(255, 0, 255));
            Draw::Circle(tgtS, 5.f, Color::White(), 1.f);
            Draw::Line(predS.x, predS.y, tgtS.x, tgtS.y, Color(255, 0, 255, 80), 1.f);
        }
    }

    // HUD
    float x = 10.f, y = 200.f;
    const float F = 11.f, L = 14.f;

    if (g_targetValid)
    {
        TextBuilder<48> tb;
        tb.put("TARGET hp=").putFloat(g_targetHp, 0).put(" dist=").putFloat(g_targetDist, 1);
        Draw::TextShadow(x, y, Color::Green(), tb.c_str(), F); y += L;

        tb.clear(); tb.put("hit=").putInt(g_cachedHitbox)
                       .put(" inRange=").putInt(g_targetDist <= g_secRange);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        tb.clear(); tb.put("pred=").putInt(g_lastPred.valid)
                       .put(" ft=").putFloat(g_lastPred.flightTime, 2);
        Draw::TextShadow(x, y, Color(255, 165, 0), tb.c_str(), F); y += L;
    }

    Entity lp = LocalPlayer();
    if (lp.IsValid())
    {
        TextBuilder<48> tb;
        Draw::TextShadow(x, y, Color::Yellow(), "-- ZARYA --", F); y += L;

        tb.clear(); tb.put("dmgRate=").putFloat(g_hpDropRate, 0).put(" hp=").putFloat(lp.GetHealth(), 0);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        SkillCooldown s1 = lp.GetSkill1Cooldown();
        tb.clear(); tb.put("Barrier cd=").putFloat(s1.current, 1).put("/").putFloat(s1.max, 1);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        SkillCooldown s2 = lp.GetSkill2Cooldown();
        tb.clear(); tb.put("ProjBarrier cd=").putFloat(s2.current, 1).put("/").putFloat(s2.max, 1);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;

        tb.clear(); tb.put("ultChg=").putFloat(lp.GetUltCharge(), 0);
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F);
    }
}

// ── Menu ──────────────────────────────────────────────────
extern "C" void on_menu()
{
    ImGui::Checkbox("Enabled", &g_enabled);
    if (!g_enabled) return;
    ImGui::Separator();

    ImGui::SliderInt("Beam Key (VK)", &g_triggerKey, 0, 255);
    ImGui::SliderInt("Orb Key (VK, 0=same)", &g_orbKey, 0, 255);
    ImGui::SliderFloat("Smoothing", &g_stiffness, 0.f, 1500.f);
    ImGui::SliderFloat("FOV Radius", &g_fovRadius, 10.f, 500.f);
    ImGui::SliderFloat("Hitbox Scale", &g_hitboxScale, 0.5f, 2.f);
    ImGui::Separator();

    ImGui::Checkbox("Auto Primary (beam)", &g_autoPrimary);
    if (g_autoPrimary)
        ImGui::SliderFloat("Primary Range (m)", &g_primaryRange, 5.f, 50.f);
    ImGui::Separator();

    ImGui::Checkbox("Auto Secondary (orb prediction)", &g_autoSecondary);
    if (g_autoSecondary)
    {
        ImGui::SliderFloat("Orb Speed (m/s)", &g_secSpeed, 10.f, 60.f);
        ImGui::SliderFloat("Orb Arc Factor", &g_secArcFactor, 0.f, 10.f);
        ImGui::SliderFloat("Secondary Range (m)", &g_secRange, 5.f, 50.f);
    }
    ImGui::Separator();

    ImGui::Checkbox("Auto Barrier (incoming damage)", &g_autoBarrier);
    if (g_autoBarrier)
        ImGui::SliderFloat("Barrier Trigger (HP/s)", &g_barrierDmgRate, 10.f, 300.f);
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
