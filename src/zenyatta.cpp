// zenyatta.cpp
// Bone aim + auto-fire (LMB / RMB volley) + auto-Discord (E/Skill2) + auto-Harmony (Shift/Skill1)

#include <xenon/SDK.hpp>
using namespace xenon;

// ── Config ────────────────────────────────────────────────
static bool  g_enabled         = true;
static Hotkey g_triggerKey(18);  // default Left Alt; click-to-bind in menu
static float g_stiffness       = 30.f;
static float g_fovRadius       = 200.f;
static bool  g_drawFov         = true;
static bool  g_drawDebug       = false;
static int   g_targetMode      = 0;     // 0=crosshair, 1=closest 3D, 2=lowest HP
static float g_targetMaxRange  = 40.f;
static bool  g_predictMovement = true;
static float g_predictionScale = 1.0f;
static float g_orbSpeed        = 40.f;   // fallback if WeaponInfo unavailable

// Primary fire (LMouse)
static bool  g_autoFire        = true;
static float g_fireRange       = 40.f;

// Volley (RMouse — hold to charge up to 5 orbs, release to fire)
static bool  g_autoVolley       = false;
static float g_volleyChargeTime = 2.0f;  // seconds to hold RMouse (~3 orbs)
static float g_volleyRange      = 20.f;
static float g_volleyInterval   = 5.0f;  // gap between volleys
static float g_volleyHoldEnd    = 0.f;
static float g_volleyCdEnd      = 0.f;

// Orb of Discord (E = Skill2 in SDK)
static bool  g_autoDiscord         = true;
static float g_discordRange        = 40.f;
static float g_discordInterval     = 3.0f;   // re-send pulse every N seconds
static float g_discordAimEnd       = 0.f;    // snap-aim phase before pressing E
static float g_discordHoldEnd      = 0.f;
static float g_discordNextAt       = 0.f;
static int32_t g_discordOnTarget   = -1;     // confirmed applied (hitbox hit during hold)
static int32_t g_discordPendingFor = -1;     // in-progress aim/hold target
static bool    g_discordHitDuringHold = false; // crosshair was on target while E held
static constexpr float kDiscordAimTime = 0.08f;
static constexpr float kDiscordHold    = 0.10f;

// Orb of Harmony (Shift = Skill1 in SDK)
// Applied when trigger is NOT held. Tracks who holds the orb (g_harmonyAppliedTo).
// When a different ally drops below threshold, CD resets so we switch immediately.
static bool  g_autoHarmony       = true;
static float g_harmonyHpPct      = 80.f;
static float g_harmonyRange      = 40.f;
static float g_harmonySnap       = 500.f;  // aim stiffness when snapping to the ally
static float g_harmonyHoldEnd    = 0.f;
static float g_harmonyCdEnd      = 0.f;
static int32_t g_harmonyTarget   = -1;
static int32_t g_harmonyAppliedTo = -1;  // who currently holds the orb
static Vector3 g_harmonyTargetPos = {};
static constexpr float kHarmonyHold = 0.12f;
static constexpr float kHarmonyCd   = 1.0f;  // min gap between re-applies

// Kick combo (Jump → Melee → gap → Melee cancel)
static bool  g_autoKick      = true;
static float g_kickRange     = 5.0f;
static float g_kickHpThresh  = 170.f;  // only kick when target HP is below this
static float g_kickJumpHold  = 0.20f;  // how long to hold Jump before+during first melee
static float g_kickT         = -1.f;   // < 0 = idle
static float g_kickCd        = 5.0f;   // cooldown between kicks (seconds)
static float g_kickCdEnd     = 0.f;    // absolute time when CD expires
// Phase boundaries computed from g_kickJumpHold at runtime (see on_frame)

// Auto Transcendence (Ult) — pop when local HP drops to/below threshold
static bool  g_autoUlt       = true;
static float g_ultHpPct      = 35.f;   // local HP% at or below which we ult
static float g_ultHoldEnd    = 0.f;    // press window
static constexpr float kUltHold = 0.10f;

// Auto melee
static bool  g_autoMelee     = true;
static float g_meleeRange    = 3.f;
static float g_meleeKillHp   = 999.f;
static float g_meleeHoldSet  = 0.10f;
static float g_meleeHold     = 0.f;

// Target cache (written on_render, read on_frame)
static int32_t g_cachedTarget    = -1;
static int32_t g_prevTarget      = -1;  // detect target switches
static bool    g_targetValid     = false;
static float   g_targetHp        = 0.f;
static float   g_targetDist      = 9999.f;
static int     g_bestBone        = Bone::Chest;
static Vector3 g_targetPos       = {};

// Hero lock
static uint64_t g_heroId         = 0;
static bool     g_heroLock       = false;

// Aim bones
static const int   kAimBones[]  = { Bone::Head, Bone::Neck, Bone::Chest, Bone::Body, Bone::Pelvis };
static const char* kBoneNames[] = { "Head", "Neck", "Chest", "Body", "Pelvis" };
static const int   kBoneCount   = 5;
static bool g_boneEnabled[5]    = { true, true, true, true, false };

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
    "zenyatta", "Zenyatta", "Xenon",
    "Bone aim + auto-fire + auto-Discord (E) + auto-Harmony (Shift).",
    "1.0", 0,
    PluginFlags::HasOverlay | PluginFlags::HasMenu
)

// ── Lifecycle ─────────────────────────────────────────────
extern "C" void on_load()
{
    g_enabled          = Config::GetBool("enabled",          true);
    g_triggerKey.Load("triggerKey");
    g_stiffness        = Config::GetFloat("stiffness",       30.f);
    g_fovRadius        = Config::GetFloat("fovRadius",       200.f);
    g_drawFov          = Config::GetBool("drawFov",          true);
    g_targetMode       = Config::GetInt("targetMode",        0);
    g_targetMaxRange   = Config::GetFloat("targetMaxRange",  40.f);
    g_predictMovement  = Config::GetBool("predictMovement",  true);
    g_predictionScale  = Config::GetFloat("predictionScale", 1.0f);
    g_orbSpeed         = Config::GetFloat("orbSpeed",        40.f);
    g_autoFire         = Config::GetBool("autoFire",         true);
    g_fireRange        = Config::GetFloat("fireRange",       40.f);
    g_autoVolley       = Config::GetBool("autoVolley",       false);
    g_volleyChargeTime = Config::GetFloat("volleyChargeTime", 2.0f);
    g_volleyRange      = Config::GetFloat("volleyRange",     20.f);
    g_volleyInterval   = Config::GetFloat("volleyInterval",  5.0f);
    g_autoDiscord      = Config::GetBool("autoDiscord",      true);
    g_discordRange     = Config::GetFloat("discordRange",    40.f);
    g_discordInterval  = Config::GetFloat("discordInterval", 3.0f);
    g_autoHarmony      = Config::GetBool("autoHarmony",      true);
    g_harmonyHpPct     = Config::GetFloat("harmonyHpPct",   80.f);
    g_harmonyRange     = Config::GetFloat("harmonyRange",   40.f);
    g_harmonySnap      = Config::GetFloat("harmonySnap",    500.f);
    g_autoUlt          = Config::GetBool("autoUlt",          true);
    g_ultHpPct         = Config::GetFloat("ultHpPct",        35.f);
    g_autoKick         = Config::GetBool("autoKick",         true);
    g_kickRange        = Config::GetFloat("kickRange",       5.0f);
    g_kickHpThresh     = Config::GetFloat("kickHpThresh",   170.f);
    g_kickJumpHold     = Config::GetFloat("kickJumpHold",   0.20f);
    g_kickCd           = Config::GetFloat("kickCd",         5.0f);
    g_autoMelee        = Config::GetBool("autoMelee",        true);
    g_meleeRange       = Config::GetFloat("meleeRange",      3.f);
    g_meleeKillHp      = Config::GetFloat("meleeKillHp",    999.f);
    g_meleeHoldSet     = Config::GetFloat("meleeHoldSet",    0.10f);
    for (int i = 0; i < kBoneCount; i++)
    {
        TextBuilder<24> k; k.put("bone").putInt(i);
        g_boneEnabled[i] = Config::GetBool(k.c_str(), i < 4);
    }
    uint32_t lo = (uint32_t)Config::GetInt("heroId_lo", 0);
    uint32_t hi = (uint32_t)Config::GetInt("heroId_hi", 0);
    g_heroId    = ((uint64_t)hi << 32) | lo;
    g_heroLock  = (g_heroId != 0);
}

extern "C" void on_unload()
{
    Config::SetBool("enabled",           g_enabled);
    g_triggerKey.Save("triggerKey");
    Config::SetFloat("stiffness",        g_stiffness);
    Config::SetFloat("fovRadius",        g_fovRadius);
    Config::SetBool("drawFov",           g_drawFov);
    Config::SetInt("targetMode",         g_targetMode);
    Config::SetFloat("targetMaxRange",   g_targetMaxRange);
    Config::SetBool("predictMovement",   g_predictMovement);
    Config::SetFloat("predictionScale",  g_predictionScale);
    Config::SetFloat("orbSpeed",         g_orbSpeed);
    Config::SetBool("autoFire",          g_autoFire);
    Config::SetFloat("fireRange",        g_fireRange);
    Config::SetBool("autoVolley",        g_autoVolley);
    Config::SetFloat("volleyChargeTime", g_volleyChargeTime);
    Config::SetFloat("volleyRange",      g_volleyRange);
    Config::SetFloat("volleyInterval",   g_volleyInterval);
    Config::SetBool("autoDiscord",       g_autoDiscord);
    Config::SetFloat("discordRange",     g_discordRange);
    Config::SetFloat("discordInterval",  g_discordInterval);
    Config::SetBool("autoHarmony",       g_autoHarmony);
    Config::SetFloat("harmonyHpPct",     g_harmonyHpPct);
    Config::SetFloat("harmonyRange",     g_harmonyRange);
    Config::SetFloat("harmonySnap",      g_harmonySnap);
    Config::SetBool("autoUlt",           g_autoUlt);
    Config::SetFloat("ultHpPct",         g_ultHpPct);
    Config::SetBool("autoKick",          g_autoKick);
    Config::SetFloat("kickRange",        g_kickRange);
    Config::SetFloat("kickHpThresh",     g_kickHpThresh);
    Config::SetFloat("kickJumpHold",     g_kickJumpHold);
    Config::SetFloat("kickCd",           g_kickCd);
    Config::SetBool("autoMelee",         g_autoMelee);
    Config::SetFloat("meleeRange",       g_meleeRange);
    Config::SetFloat("meleeKillHp",      g_meleeKillHp);
    Config::SetFloat("meleeHoldSet",     g_meleeHoldSet);
    for (int i = 0; i < kBoneCount; i++)
    {
        TextBuilder<24> k; k.put("bone").putInt(i);
        Config::SetBool(k.c_str(), g_boneEnabled[i]);
    }
    Config::SetInt("heroId_lo", (int32_t)(g_heroId & 0xFFFFFFFF));
    Config::SetInt("heroId_hi", (int32_t)(g_heroId >> 32));
    Config::Save();
}

extern "C" void on_hero_changed(uint64_t)
{
    g_cachedTarget    = -1;
    g_targetValid     = false;
    g_discordOnTarget = -1;
    g_discordHoldEnd  = 0.f;
    g_discordNextAt   = 0.f;
    g_harmonyTarget    = -1;
    g_harmonyAppliedTo = -1;
    g_harmonyHoldEnd   = 0.f;
    g_harmonyCdEnd     = 0.f;
    g_volleyHoldEnd   = 0.f;
    g_volleyCdEnd     = 0.f;
    g_ultHoldEnd      = 0.f;
    g_kickT           = -1.f;
    g_kickCdEnd       = 0.f;
    g_meleeHold       = 0.f;
    AimResetSmoothing();
}

// ── Frame Logic ───────────────────────────────────────────
extern "C" void on_frame(float dt)
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Zenyatta) return; }  // dormant on any other hero

    g_triggerKey.Update();

    float now  = GetTime();
    bool  held = g_triggerKey.IsDown();

    bool kickRunning = (g_kickT >= 0.f);

    // ── Auto Transcendence (Ult) — survival, fires regardless of trigger ──
    // Pops when local HP% drops to/below threshold and ult is ready.
    if (g_ultHoldEnd > 0.f)
    {
        if (now < g_ultHoldEnd) PressGameButton(GameButton::Ult);
        else { ReleaseGameButton(GameButton::Ult); g_ultHoldEnd = 0.f; }
    }
    else if (g_autoUlt && IsUltReady() && !IsUltActive())
    {
        Entity localE = LocalPlayer();
        if (localE.IsValid())
        {
            float hpPct = localE.GetHealthPercent();
            if (hpPct > 0.f && hpPct <= g_ultHpPct)
                g_ultHoldEnd = now + kUltHold;
        }
    }

    // ── Harmony pulse (Skill1 = Shift) ───────────────────
    // Suppressed during kick combo so Shift doesn't interfere with jump/melee sequence.
    bool rmbHeld = IsKeyDown(2);  // VK_RBUTTON
    if (g_autoHarmony && held && !rmbHeld && !kickRunning)
    {
        if (g_harmonyHoldEnd > 0.f)
        {
            if (now < g_harmonyHoldEnd)
            {
                if (g_harmonyTargetPos.IsValid())
                    AimAtPosition(g_harmonyTargetPos, g_harmonySnap);
                PressGameButton(GameButton::Skill1);
            }
            else
            {
                ReleaseGameButton(GameButton::Skill1);
                g_harmonyHoldEnd = 0.f;
            }
        }
        else if (g_harmonyTarget >= 0 && now >= g_harmonyCdEnd)
        {
            g_harmonyHoldEnd   = now + kHarmonyHold;
            g_harmonyCdEnd     = now + kHarmonyCd;
            g_harmonyAppliedTo = g_harmonyTarget;
        }
        else
            ReleaseGameButton(GameButton::Skill1);
    }
    else
    {
        ReleaseGameButton(GameButton::Skill1);
        g_harmonyHoldEnd = 0.f;
    }

    // ── Auto Discord (Skill2 = E) — fires regardless of trigger ──
    // Reset confirmation immediately when target changes so discord reapplies.
    if (g_cachedTarget != g_prevTarget)
    {
        g_discordOnTarget      = -1;
        g_discordAimEnd        = 0.f;
        g_discordHoldEnd       = 0.f;
        g_discordPendingFor    = -1;
        g_discordHitDuringHold = false;
        ReleaseGameButton(GameButton::Skill2);
        g_prevTarget = g_cachedTarget;
    }
    if (g_discordHoldEnd > 0.f)
    {
        if (!held || !g_targetValid || g_cachedTarget < 0 || g_cachedTarget != g_discordPendingFor || kickRunning)
        {
            ReleaseGameButton(GameButton::Skill2);
            g_discordHoldEnd    = 0.f;
            g_discordPendingFor = -1;
        }
        else if (now < g_discordHoldEnd)
        {
            AimAtBone(g_cachedTarget, g_bestBone, 2000.f);
            PressGameButton(GameButton::Skill2);
            if (AimHitsHitbox(g_cachedTarget, 1.5f) >= 0)
                g_discordHitDuringHold = true;
        }
        else
        {
            ReleaseGameButton(GameButton::Skill2);
            g_discordHoldEnd    = 0.f;
            // Always mark as applied — if hitbox was positive use full interval,
            // otherwise retry quickly so it keeps pressing until it sticks.
            g_discordOnTarget   = g_discordPendingFor;
            g_discordNextAt     = now + (g_discordHitDuringHold ? g_discordInterval : 0.5f);
            g_discordPendingFor    = -1;
            g_discordHitDuringHold = false;
        }
    }
    else if (g_discordAimEnd > 0.f)
    {
        if (!held || !g_targetValid || g_cachedTarget < 0 || g_cachedTarget != g_discordPendingFor || kickRunning)
        {
            ReleaseGameButton(GameButton::Skill2);
            g_discordAimEnd     = 0.f;
            g_discordPendingFor = -1;
        }
        else
        {
            AimAtBone(g_cachedTarget, g_bestBone, 2000.f);
            ReleaseGameButton(GameButton::Skill2);
            if (now >= g_discordAimEnd)
            {
                g_discordHoldEnd      = now + kDiscordHold;
                g_discordHitDuringHold = false;
                g_discordAimEnd       = 0.f;
            }
        }
    }
    else if (!kickRunning && g_autoDiscord && held && g_targetValid && g_cachedTarget >= 0 && g_targetDist <= g_discordRange)
    {
        bool needsDiscord = (g_discordOnTarget != g_cachedTarget) || (now >= g_discordNextAt);
        if (needsDiscord)
        {
            g_discordPendingFor = g_cachedTarget;
            g_discordAimEnd     = now + kDiscordAimTime;
            AimAtBone(g_cachedTarget, g_bestBone, 2000.f);
        }
        ReleaseGameButton(GameButton::Skill2);
    }
    else
        ReleaseGameButton(GameButton::Skill2);

    // Allow attacks if discord is on cooldown (can't cast it anyway).
    auto skill2Cd    = LocalPlayer().GetSkill2Cooldown();
    bool discordOnCd = skill2Cd.enabled && skill2Cd.current > 0.f;
    bool discordApplied = (!g_autoDiscord || discordOnCd || g_discordOnTarget == g_cachedTarget);

    // ── Bail if not active ────────────────────────────────
    bool volleyActive = (g_volleyHoldEnd > 0.f);
    if (!held && !volleyActive)
    {
        ReleaseGameButton(GameButton::LMouse);
        ReleaseGameButton(GameButton::RMouse);
        ReleaseGameButton(GameButton::Melee);
        ReleaseGameButton(GameButton::Jump);
        g_meleeHold = 0.f;
        if (!g_targetValid) AimResetSmoothing();
        return;
    }

    if (!g_targetValid || g_cachedTarget < 0)
    {
        ReleaseGameButton(GameButton::LMouse);
        ReleaseGameButton(GameButton::RMouse);
        ReleaseGameButton(GameButton::Melee);
        ReleaseGameButton(GameButton::Jump);
        g_meleeHold = 0.f;
        return;
    }

    // ── Aim assist ────────────────────────────────────────
    {
        bool aimed = false;
        if (g_predictMovement)
        {
            Entity tgtE(g_cachedTarget);
            WeaponInfo wi{};
            float speed = (GetWeaponInfo(InputFlag::PrimaryFire, wi) && wi.projectileSpeed > 0.f)
                          ? wi.projectileSpeed : g_orbSpeed;
            if (tgtE.IsValid() && speed > 0.f)
            {
                float   travel  = (g_targetDist / speed) * g_predictionScale;
                Vector3 entPos  = tgtE.GetPosition();
                Vector3 bone    = tgtE.GetBonePos(g_bestBone);
                Vector3 pred    = tgtE.PredictPosition(travel);
                if (bone.IsValid() && entPos.IsValid() && pred.IsValid())
                {
                    Vector3 aim;
                    aim.x = pred.x + (bone.x - entPos.x);
                    aim.y = pred.y + (bone.y - entPos.y);
                    aim.z = pred.z + (bone.z - entPos.z);
                    AimAtPosition(aim, g_stiffness);
                    aimed = true;
                }
            }
        }
        if (!aimed) AimAtBone(g_cachedTarget, g_bestBone, g_stiffness);
    }

    // ── Kick combo (Jump → Melee → gap → Melee) ──────────
    // Requires: discord applied, crosshair on target, in range, HP low, CD ready.
    bool kickActive = (g_kickT >= 0.f);
    if (!kickActive && g_autoKick && held && g_targetValid
        && g_targetDist <= g_kickRange && g_targetHp < g_kickHpThresh
        && now >= g_kickCdEnd && g_discordHoldEnd <= 0.f)
    {
        g_kickT    = 0.f;
        kickActive = true;
    }
    if (kickActive)
    {
        g_kickT += dt;
        float t  = g_kickT;
        float p1 = g_kickJumpHold;
        float p2 = p1 + 0.12f;
        float p3 = p2 + 0.05f;
        float p4 = p3 + 0.12f;
        if (t < p1)
        {
            PressGameButton(GameButton::Jump);
            ReleaseGameButton(GameButton::Melee);
        }
        else if (t < p2)
        {
            PressGameButton(GameButton::Jump);
            PressGameButton(GameButton::Melee);
        }
        else if (t < p3)
        {
            ReleaseGameButton(GameButton::Jump);
            ReleaseGameButton(GameButton::Melee);
        }
        else if (t < p4)
        {
            PressGameButton(GameButton::Melee);
        }
        else
        {
            ReleaseGameButton(GameButton::Melee);
            ReleaseGameButton(GameButton::Jump);
            g_kickT     = -1.f;
            g_kickCdEnd = now + g_kickCd;
            kickActive  = false;
        }
    }
    else
        ReleaseGameButton(GameButton::Jump);

    // Kick owns all remaining buttons while running
    if (kickActive)
    {
        ReleaseGameButton(GameButton::LMouse);
        ReleaseGameButton(GameButton::RMouse);
        return;
    }

    // ── Auto melee ────────────────────────────────────────
    // inKickZone only suppresses when kick will actually fire (range + HP + CD).
    // If HP is above kick threshold, melee is allowed even at close range.
    bool inKickZone = g_autoKick && g_targetValid && g_targetDist <= g_kickRange
                      && g_targetHp < g_kickHpThresh && now >= g_kickCdEnd;
    bool meleeCanHit = discordApplied && g_autoMelee && held && g_targetValid
                       && g_targetDist <= g_meleeRange && g_targetHp <= g_meleeKillHp;
    if (!inKickZone && meleeCanHit)
    {
        if (g_meleeHold <= 0.f) g_meleeHold = g_meleeHoldSet;
        if (g_meleeHold > 0.f) { PressGameButton(GameButton::Melee); g_meleeHold -= dt; }
        else                     ReleaseGameButton(GameButton::Melee);
    }
    else
    {
        ReleaseGameButton(GameButton::Melee);
        g_meleeHold = 0.f;
    }

    // Orbs only fire when not meleeing (melee has priority over orb fire)
    bool meleeFiring = (!inKickZone && meleeCanHit && g_meleeHold > 0.f);

    // ── Auto Volley (RMouse hold → release fires orbs) ───
    bool volleyFiring = false;
    if (g_autoVolley && !meleeFiring)
    {
        if (g_volleyHoldEnd > 0.f)
        {
            if (now < g_volleyHoldEnd && held && g_targetValid)
            {
                PressGameButton(GameButton::RMouse);
                volleyFiring = true;
            }
            else
            {
                ReleaseGameButton(GameButton::RMouse);
                g_volleyHoldEnd = 0.f;
                g_volleyCdEnd   = now + g_volleyInterval;
            }
        }
        else if (discordApplied && held && g_targetValid && g_targetDist <= g_volleyRange && now >= g_volleyCdEnd)
        {
            g_volleyHoldEnd = now + g_volleyChargeTime;
            PressGameButton(GameButton::RMouse);
            volleyFiring = true;
        }
        else
            ReleaseGameButton(GameButton::RMouse);
    }
    else
        ReleaseGameButton(GameButton::RMouse);

    // ── Primary fire (LMouse) ─────────────────────────────
    // Fire whenever we're not actively casting Discord (only the E press window
    // conflicts). Don't wait for Discord to *confirm* — that retries on hitbox
    // misses and was causing orbs to stop firing intermittently.
    bool discordCasting = (g_discordHoldEnd > 0.f) || (g_discordAimEnd > 0.f);
    if (!discordCasting && !meleeFiring && g_autoFire && held && g_targetValid
        && g_targetDist <= g_fireRange && !volleyFiring && !kickActive && !inKickZone)
        PressGameButton(GameButton::LMouse);
    else
        ReleaseGameButton(GameButton::LMouse);
}

// ── Render (target lock + HUD) ────────────────────────────
extern "C" void on_render()
{
    if (!g_enabled || !IsIngame()) return;
    { Entity lp = LocalPlayer(); if (!lp.IsValid() || lp.GetHeroId() != HeroId::Zenyatta) return; }  // dormant on any other hero

    Vector2 sz = ScreenSize();
    if (sz.x <= 0 || sz.y <= 0) return;
    Vector2 center = { sz.x * 0.5f, sz.y * 0.5f };

    if (g_drawFov)
        Draw::Circle(center, g_fovRadius, Color(255, 255, 255, 60), 1.f);

    if (g_drawDebug)
    {
        float dx = 10.f, dy = 10.f, lh = 16.f;
        auto line = [&](const char* txt, Color col) {
            Draw::TextShadow(dx, dy, col, txt, 14);
            dy += lh;
        };

        bool held_dbg       = g_triggerKey.IsDown();
        auto dbgSk2Cd       = LocalPlayer().GetSkill2Cooldown();
        bool dbgDiscordOnCd = dbgSk2Cd.enabled && dbgSk2Cd.current > 0.f;
        bool discordApplied = (g_discordOnTarget == g_cachedTarget) || dbgDiscordOnCd;
        bool kickActive     = g_kickT >= 0.f;
        bool inRange        = g_targetValid && g_targetDist <= g_kickRange;
        bool hpOk           = g_targetValid && g_targetHp < g_kickHpThresh;
        float kickCdRemain  = g_kickCdEnd - GetTime();
        bool cdOk           = kickCdRemain <= 0.f;
        bool discordDone    = g_discordHoldEnd <= 0.f;

        {
            TextBuilder<72> t;
            t.put("Discord: onTgt=").putInt(g_discordOnTarget)
             .put(" applied=").putInt(discordApplied?1:0)
             .put(" onCd=").putInt(dbgDiscordOnCd?1:0);
            line(t.c_str(), discordApplied ? Color::Green() : Color(255,80,80));
        }
        {
            TextBuilder<80> t;
            t.put("Kick: held=").putInt(held_dbg?1:0)
             .put(" range=").putInt(inRange?1:0)
             .put(" hp=").putInt(hpOk?1:0)
             .put(" cd=").putInt(cdOk?1:0)
             .put(" dDone=").putInt(discordDone?1:0);
            bool allOk = held_dbg && inRange && hpOk && cdOk && discordDone && discordApplied;
            line(t.c_str(), allOk ? Color::Green() : Color(255,80,80));
        }
        {
            TextBuilder<64> t;
            t.put("  dist=").putFloat(g_targetDist,2)
             .put(" hp=").putFloat(g_targetHp,1)
             .put(" cd=").putFloat(kickCdRemain,1)
             .put(" kT=").putFloat(g_kickT,2);
            line(t.c_str(), Color(200,200,200));
        }
    }

    // ── Harmony ally scan (runs every frame regardless of trigger) ──
    // Finds the lowest-HP ally below threshold within range.
    // If the best target is different from who currently holds the orb,
    // the cooldown resets so on_frame fires the switch immediately.
    {
        Entity  localE  = LocalPlayer();
        Vector3 myPos   = localE.IsValid() ? localE.GetPosition() : Vector3{};
        int32_t bestIdx = -1;
        float   bestHpPct = 99999.f;
        Vector3 bestPos   = {};

        for (Entity p : Players())
        {
            if (!p.IsAlive() || p.IsLocal() || !p.IsAlly()) continue;
            float d = WorldDist(myPos, p.GetPosition());
            if (d > g_harmonyRange) continue;
            float hpPct = p.GetHealthPercent();
            // Only consider allies already in FOV — avoids aim drifting off-screen
            Vector3 chestW = p.GetBonePos(Bone::Chest);
            if (!chestW.IsValid()) chestW = p.GetPosition();
            Vector2 chestS;
            if (!WorldToScreen(chestW, chestS)) continue;
            if (ScreenDist(chestS, center) > g_fovRadius) continue;
            if (hpPct < bestHpPct)
            {
                bestHpPct = hpPct;
                bestIdx   = p.Index();
                bestPos   = chestW;
            }
        }

        // Target changed → reset CD so on_frame switches immediately
        if (bestIdx != g_harmonyAppliedTo)
            g_harmonyCdEnd = 0.f;

        g_harmonyTarget    = bestIdx;
        g_harmonyTargetPos = bestPos;
    }

    bool held        = g_triggerKey.IsDown();
    bool volleyActive = (g_volleyHoldEnd > 0.f);

    if (!held && !volleyActive)
    {
        g_targetValid  = false;
        g_cachedTarget = -1;
        return;
    }

    Entity local = LocalPlayer();
    if (!local.IsValid()) return;
    Vector3 myPos = local.GetPosition();

    // ── Enemy scan ────────────────────────────────────────
    int32_t bestIdx   = -1;
    float   bestScore = 99999.f;
    float   bestHp    = 0.f;
    float   bestDist  = 9999.f;
    Entity  bestEnt;

    for (Entity p : Players())
    {
        if (!p.IsAlive() || p.IsLocal() || !p.IsEnemy()) continue;
        if (!p.IsVisible()) continue;  // filters enemies behind walls

        // Use the game's own targeting math (like venture): closest bone within
        // FOV + its pixel distance to the ACTUAL crosshair — more accurate than
        // measuring from assumed screen-center.
        int boneId = p.GetClosestBoneInFov(g_fovRadius);
        if (boneId < 0) continue;       // not in FOV
        float crossSd = p.GetFovTo(boneId);

        float dist = WorldDist(myPos, p.GetPosition());
        if (dist > g_targetMaxRange) continue;

        float hp = p.GetHealth();
        float score;
        if      (g_targetMode == 1) score = dist;     // closest to me
        else if (g_targetMode == 2) score = hp;       // lowest HP
        else                        score = crossSd;  // closest to crosshair

        if (score < bestScore)
        {
            bestScore = score;
            bestIdx   = p.Index();
            bestHp    = hp;
            bestDist  = dist;
            bestEnt   = p;
        }
    }

    if (bestIdx >= 0)
    {
        if (bestIdx != g_cachedTarget) AimResetSmoothing();
        g_targetValid  = true;
        g_targetHp     = bestHp;
        g_targetDist   = bestDist;
        g_targetPos    = bestEnt.GetPosition();
        g_cachedTarget = bestIdx;

        float closestBone = 99999.f;
        g_bestBone = Bone::Chest;
        for (int b = 0; b < kBoneCount; b++)
        {
            if (!g_boneEnabled[b]) continue;
            Vector3 bw = bestEnt.GetBonePos(kAimBones[b]);
            if (!bw.IsValid()) continue;
            Vector2 bs;
            if (!WorldToScreen(bw, bs)) continue;
            float d = ScreenDist(bs, center);
            if (d < closestBone) { closestBone = d; g_bestBone = kAimBones[b]; }
        }
    }
    else
    {
        if (g_targetValid) AimResetSmoothing();
        g_targetValid  = false;
        g_cachedTarget = -1;
        g_targetDist   = 9999.f;
    }

    // ── Draw target marker ────────────────────────────────
    if (g_targetValid && bestEnt.IsValid())
    {
        Vector3 headW = bestEnt.GetBonePos(Bone::Head);
        Vector2 headS;
        if (headW.IsValid() && WorldToScreen(headW, headS))
        {
            Draw::Circle(headS, 10.f, Color::Cyan(), 1.5f);
            Draw::Line(headS.x - 14.f, headS.y, headS.x + 14.f, headS.y, Color::Cyan(), 1.f);
            Draw::Line(headS.x, headS.y - 14.f, headS.x, headS.y + 14.f, Color::Cyan(), 1.f);
        }
    }

    // ── HUD ───────────────────────────────────────────────
    float x = 10.f, y = 200.f;
    const float F = 11.f, L = 14.f;
    {
        TextBuilder<32> tb; tb.put("-- ZENYATTA --");
        Draw::TextShadow(x, y, Color::Yellow(), tb.c_str(), F); y += L;
    }
    if (g_targetValid)
    {
        TextBuilder<64> tb;
        tb.put("TGT hp=").putFloat(g_targetHp, 0)
          .put(" dist=").putFloat(g_targetDist, 1)
          .put(g_discordOnTarget == g_cachedTarget ? " [DISC]" : "");
        Draw::TextShadow(x, y, Color::Green(), tb.c_str(), F); y += L;
    }
    {
        TextBuilder<64> tb;
        tb.put("harmony=").putInt(g_harmonyTarget);
        if (g_volleyHoldEnd > 0.f) tb.put(" VOLLEY");
        if (g_kickT >= 0.f) tb.put(" KICK");
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;
    }
    {
        bool s1 = IsSkill1Active();
        bool s2 = IsSkill2Active();
        TextBuilder<32> tb;
        tb.put("S1=").put(s1 ? "Y" : "N").put(" S2=").put(s2 ? "Y" : "N");
        Draw::TextShadow(x, y, Color::White(), tb.c_str(), F); y += L;
    }
}

// ── Menu ──────────────────────────────────────────────────
extern "C" void on_menu()
{
    if (ImGui::CollapsingHeader("Zenyatta"))
    {
        ImGui::Checkbox("Enabled", &g_enabled);
        if (!g_enabled) return;
        ImGui::Separator();

        g_triggerKey.Render("Trigger Key");
        ImGui::SliderFloat("Smoothing",      &g_stiffness,  0.f, 5000.f);
        ImGui::SliderFloat("FOV Radius",     &g_fovRadius,  10.f, 500.f);
        ImGui::Checkbox("Draw FOV",          &g_drawFov);
        ImGui::Checkbox("Debug Overlay",     &g_drawDebug);
        ImGui::Combo("Target Priority", &g_targetMode,
                     "Closest to Crosshair\0Closest to Me (3D)\0Lowest HP\0");
        ImGui::SliderFloat("Target Max Range (m)", &g_targetMaxRange, 5.f, 60.f);
        ImGui::Checkbox("Predict Movement (lead aim)", &g_predictMovement);
        if (g_predictMovement)
        {
            ImGui::SliderFloat("Prediction Scale", &g_predictionScale, 0.f, 2.f);
            ImGui::SliderFloat("Orb Speed (m/s fallback)", &g_orbSpeed, 10.f, 80.f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Auto Fire (LMB)", &g_autoFire);
        if (g_autoFire)
            ImGui::SliderFloat("Fire Range (m)", &g_fireRange, 5.f, 60.f);
        ImGui::Separator();

        ImGui::Checkbox("Auto Volley (RMB hold+release)", &g_autoVolley);
        if (g_autoVolley)
        {
            ImGui::SliderFloat("Volley Charge Time (s)", &g_volleyChargeTime, 0.5f, 3.5f);
            ImGui::SliderFloat("Volley Range (m)",       &g_volleyRange,      5.f,  40.f);
            ImGui::SliderFloat("Volley Interval (s)",    &g_volleyInterval,   1.f,  15.f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Auto Discord (E / Skill2)", &g_autoDiscord);
        if (g_autoDiscord)
        {
            ImGui::SliderFloat("Discord Range (m)",      &g_discordRange,    5.f, 60.f);
            ImGui::SliderFloat("Discord Reapply (s)",    &g_discordInterval, 0.5f, 10.f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Auto Harmony (Shift / Skill1)", &g_autoHarmony);
        if (g_autoHarmony)
        {
            ImGui::SliderFloat("Harmony HP Threshold %", &g_harmonyHpPct, 10.f, 99.f);
            ImGui::SliderFloat("Harmony Range (m)",      &g_harmonyRange, 5.f,  60.f);
            ImGui::SliderFloat("Harmony Snap",           &g_harmonySnap,  0.f,  5000.f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Auto Ult (Transcendence)", &g_autoUlt);
        if (g_autoUlt)
            ImGui::SliderFloat("Ult at HP % (or below)", &g_ultHpPct, 5.f, 99.f);
        ImGui::Separator();

        ImGui::Checkbox("Auto Kick (Jump+Melee+Melee)", &g_autoKick);
        if (g_autoKick)
        {
            ImGui::SliderFloat("Kick Range (m)",     &g_kickRange,    0.5f, 8.f);
            ImGui::SliderFloat("Kick HP Threshold",  &g_kickHpThresh, 50.f, 500.f);
            ImGui::SliderFloat("Kick Jump Hold (s)", &g_kickJumpHold, 0.05f, 0.5f);
            ImGui::SliderFloat("Kick Cooldown (s)",  &g_kickCd,       1.0f,  10.0f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Auto Melee", &g_autoMelee);
        if (g_autoMelee)
        {
            ImGui::SliderFloat("Melee Range (m)",  &g_meleeRange,   0.5f, 8.f);
            ImGui::SliderFloat("Melee Kill HP",    &g_meleeKillHp,  0.f,  999.f);
            ImGui::SliderFloat("Melee Hold (s)",   &g_meleeHoldSet, 0.05f, 0.3f);
        }
        ImGui::Separator();

        ImGui::Text("Aim Bones:");
        for (int b = 0; b < kBoneCount; b++)
            ImGui::Checkbox(kBoneNames[b], &g_boneEnabled[b]);
        ImGui::Separator();

        TextBuilder<48> hb;
        hb.put("Hero ID: ").putInt((int)g_heroId)
          .put(g_heroId == 0 ? "  (any)" : "  (locked)");
        ImGui::Text(hb.c_str());
        bool prevLock = g_heroLock;
        ImGui::Checkbox("Lock to current hero", &g_heroLock);
        if (g_heroLock != prevLock)
        {
            if (g_heroLock) g_heroId = GetCurrentHero();
            else            g_heroId = 0;
        }
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Discord Debug"))
        {
            Entity localDbg = LocalPlayer();
            auto s1cd  = localDbg.GetSkill1Cooldown();
            auto s1dur = localDbg.GetSkill1Duration();
            auto s2cd  = localDbg.GetSkill2Cooldown();
            auto s2dur = localDbg.GetSkill2Duration();

            ImGui::Text("== Local Player ==");
            {
                TextBuilder<80> t;
                t.put("Skill1 CD:  cur=").putFloat(s1cd.current,2).put(" max=").putFloat(s1cd.max,2).put(" en=").putInt(s1cd.enabled?1:0);
                ImGui::Text(t.c_str());
            }
            {
                TextBuilder<80> t;
                t.put("Skill1 Dur: cur=").putFloat(s1dur.current,2).put(" max=").putFloat(s1dur.max,2).put(" en=").putInt(s1dur.enabled?1:0);
                ImGui::Text(t.c_str());
            }
            {
                TextBuilder<80> t;
                t.put("Skill2 CD:  cur=").putFloat(s2cd.current,2).put(" max=").putFloat(s2cd.max,2).put(" en=").putInt(s2cd.enabled?1:0);
                ImGui::Text(t.c_str());
            }
            {
                TextBuilder<80> t;
                t.put("Skill2 Dur: cur=").putFloat(s2dur.current,2).put(" max=").putFloat(s2dur.max,2).put(" en=").putInt(s2dur.enabled?1:0);
                ImGui::Text(t.c_str());
            }
            {
                TextBuilder<48> t;
                t.put("Sk1Active=").putInt(localDbg.IsSkill1Active()?1:0)
                 .put(" Sk2Active=").putInt(localDbg.IsSkill2Active()?1:0);
                ImGui::Text(t.c_str());
            }

            if (g_targetValid && g_cachedTarget >= 0)
            {
                ImGui::Text("== Current Target ==");
                Entity tgtDbg(g_cachedTarget);
                auto ts1cd  = tgtDbg.GetSkill1Cooldown();
                auto ts1dur = tgtDbg.GetSkill1Duration();
                auto ts2cd  = tgtDbg.GetSkill2Cooldown();
                auto ts2dur = tgtDbg.GetSkill2Duration();
                {
                    TextBuilder<80> t;
                    t.put("Skill1 CD:  cur=").putFloat(ts1cd.current,2).put(" max=").putFloat(ts1cd.max,2).put(" en=").putInt(ts1cd.enabled?1:0);
                    ImGui::Text(t.c_str());
                }
                {
                    TextBuilder<80> t;
                    t.put("Skill1 Dur: cur=").putFloat(ts1dur.current,2).put(" max=").putFloat(ts1dur.max,2).put(" en=").putInt(ts1dur.enabled?1:0);
                    ImGui::Text(t.c_str());
                }
                {
                    TextBuilder<80> t;
                    t.put("Skill2 CD:  cur=").putFloat(ts2cd.current,2).put(" max=").putFloat(ts2cd.max,2).put(" en=").putInt(ts2cd.enabled?1:0);
                    ImGui::Text(t.c_str());
                }
                {
                    TextBuilder<80> t;
                    t.put("Skill2 Dur: cur=").putFloat(ts2dur.current,2).put(" max=").putFloat(ts2dur.max,2).put(" en=").putInt(ts2dur.enabled?1:0);
                    ImGui::Text(t.c_str());
                }
                {
                    TextBuilder<48> t;
                    t.put("Sk1Active=").putInt(tgtDbg.IsSkill1Active()?1:0)
                     .put(" Sk2Active=").putInt(tgtDbg.IsSkill2Active()?1:0);
                    ImGui::Text(t.c_str());
                }
            }
            else
            {
                ImGui::Text("== No Target ==");
            }

            {
                TextBuilder<64> t;
                t.put("discordOnTarget=").putInt(g_discordOnTarget)
                 .put(" cachedTarget=").putInt(g_cachedTarget);
                ImGui::Text(t.c_str());
            }
            {
                bool applied = g_discordOnTarget == g_cachedTarget;
                TextBuilder<48> t;
                t.put("discordApplied=").putInt(applied?1:0);
                ImGui::Text(t.c_str());
            }

            ImGui::Text("== Kick Conditions ==");
            {
                bool kickActive  = g_kickT >= 0.f;
                bool inRange     = g_targetValid && g_targetDist <= g_kickRange;
                bool hpOk        = g_targetValid && g_targetHp < g_kickHpThresh;
                bool cdOk        = g_kickCdEnd <= 0.f;
                bool discordDone = g_discordHoldEnd <= 0.f;
                bool hitbox      = g_targetValid && g_cachedTarget >= 0 && AimHitsHitbox(g_cachedTarget, 1.0f) >= 0;
                {
                    TextBuilder<80> t;
                    t.put("kickActive=").putInt(kickActive?1:0)
                     .put(" inRange=").putInt(inRange?1:0)
                     .put(" hpOk=").putInt(hpOk?1:0);
                    ImGui::Text(t.c_str());
                }
                {
                    TextBuilder<80> t;
                    t.put("cdOk=").putInt(cdOk?1:0)
                     .put(" discordDone=").putInt(discordDone?1:0)
                     .put(" hitbox=").putInt(hitbox?1:0);
                    ImGui::Text(t.c_str());
                }
                {
                    TextBuilder<64> t;
                    t.put("dist=").putFloat(g_targetDist,2)
                     .put(" hp=").putFloat(g_targetHp,1)
                     .put(" thresh=").putFloat(g_kickHpThresh,1);
                    ImGui::Text(t.c_str());
                }
            }
        }
    }
}
